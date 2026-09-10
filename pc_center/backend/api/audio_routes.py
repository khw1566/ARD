import os
import sys
import time
import numpy as np
import threading
import requests
from flask import Blueprint, request, jsonify

from config.config_manager import config
from network.sse_manager import sse_manager
from core.state_manager import state_manager
from network.tcp_client import send_tcp_command_async
from audio.tts_engine import tts_to_pcm_and_send
from audio.voice_queue import voice_queue_manager
from audio.music_player import music_player
from audio.stt_engine import process_voice_command

audio_bp = Blueprint('audio', __name__)

@audio_bp.route('/api/soundmcu/chat', methods=['POST'])
def manual_chat():
    from ai.llm_manager import get_gemini_conversational_response
    data = request.json
    text = data.get("text", "")
    client_turn_id = data.get("turn_id", "")
    if not text:
        return jsonify({"status": "error", "message": "No text"}), 400
    
    def run_chat_pipeline():
        try:
            t_start = time.perf_counter()
            turn_id = client_turn_id if client_turn_id else f"t_{int(t_start * 1000)}"
            
            state_manager.device_stats["SoundMCU"]["status"] = "Processing"
            ai_response = get_gemini_conversational_response(text)
            t_ai_end = time.perf_counter()
            ai_ms = int((t_ai_end - t_start) * 1000)
            
            sse_manager.broadcast_system_log(f"AI (Manual): {ai_response[:30]}...", "Gemini")
            
            audio_url = tts_to_pcm_and_send(ai_response)
            t_tts_end = time.perf_counter()
            tts_ms = int((t_tts_end - t_ai_end) * 1000)
            total_ms = int((t_tts_end - t_start) * 1000)
            
            res_payload = {
                "type": "update",
                "turn_id": turn_id,
                "analysis": {"type": "voice", "role": "AI", "text": ai_response, "audio_url": audio_url},
                "timings": {"stt": 0, "ai": ai_ms}
            }
            sse_manager.broadcast_event(res_payload)
            
            if config.get("soundmcu", {}).get("ip"):
                state_manager.device_stats["SoundMCU"]["status"] = "Speaking"
                
            final_payload = {
                "type": "update",
                "turn_id": turn_id,
                "timings": {
                    "stt": 0,
                    "ai": ai_ms,
                    "tts": tts_ms,
                    "total": total_ms
                },
                "audio_url": audio_url
            }
            sse_manager.broadcast_event(final_payload)
            state_manager.device_stats["SoundMCU"]["status"] = "Idle"
            sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
        except Exception as e:
            print(f"Chat Pipeline Error: {e}")
            state_manager.device_stats["SoundMCU"]["status"] = "Idle"
            sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})

    threading.Thread(target=run_chat_pipeline, daemon=True).start()
    return jsonify({"status": "queued"})

@audio_bp.route('/api/soundmcu/test_emotion_tts', methods=['POST'])
def test_emotion_tts():
    try:
        data = request.json or {}
        text = data.get("text", "안녕하세요! 오늘 기분이 정말 어떠신가요?")
        style = data.get("style", "cheerful")
        style_degree = float(data.get("style_degree", 1.5))
        
        api_key = config["soundmcu"].get("azure_api_key", "")
        region = config["soundmcu"].get("azure_region", "koreacentral")
        voice_id = config["soundmcu"]["tts_voice"]
        pitch_val = config["soundmcu"]["tts_pitch"]
        rate_val = config["soundmcu"]["tts_rate"]
        
        if not api_key:
            return jsonify({"status": "error", "message": "Azure Speech API Key가 설정되지 않았습니다."}), 400
            
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            
        audio_dir = os.path.join(base_dir, "static", "audio")
        os.makedirs(audio_dir, exist_ok=True)
        audio_file = os.path.join(audio_dir, "test_emotion.wav")
        
        pitch_str = f"{'+' if pitch_val >= 0 else ''}{pitch_val}%"
        rate_str = f"{'+' if rate_val >= 0 else ''}{rate_val}%"
        
        ssml = (
            f"<speak version='1.0' xml:lang='ko-KR' xmlns='http://www.w3.org/2001/10/synthesis' xmlns:mstts='https://www.w3.org/2001/mstts'>"
            f"<voice name='{voice_id}'>"
            f"<mstts:express-as style='{style}' styledegree='{style_degree}'>"
            f"<prosody pitch='{pitch_str}' rate='{rate_str}'>{text}</prosody>"
            f"</mstts:express-as>"
            f"</voice></speak>"
        )
        
        url = f"https://{region}.tts.speech.microsoft.com/cognitiveservices/v1"
        headers = {
            "Ocp-Apim-Subscription-Key": api_key,
            "Content-Type": "application/ssml+xml",
            "X-Microsoft-OutputFormat": "riff-16khz-16bit-mono-pcm",
            "User-Agent": "ARD-PC-Center"
        }
        
        res = requests.post(url, headers=headers, data=ssml.encode('utf-8'), timeout=10)
        if res.status_code == 200:
            with open(audio_file, "wb") as f:
                f.write(res.content)
            audio_url = f"/static/audio/test_emotion.wav?t={int(time.time())}"
            return jsonify({"status": "success", "audio_url": audio_url})
        else:
            return jsonify({"status": "error", "message": f"Azure HTTP Error: {res.status_code} - {res.text}"}), 500
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@audio_bp.route('/api/pc_mic/voice', methods=['POST'])
def receive_pc_mic_voice():
    try:
        audio_data = request.data
        if not audio_data:
            return jsonify({"status": "error", "message": "No audio data"}), 400
            
        chunk = np.frombuffer(audio_data, dtype=np.int16)
        
        if len(chunk) < 1600:
            return jsonify({"status": "error", "message": "Audio too short"}), 400

        sse_manager.broadcast_system_log(f"PC Mic Input: {len(chunk)} samples received", "Sound")
        
        state_manager.device_stats["SoundMCU"]["status"] = "Processing"
        sse_manager.broadcast_event({"type": "user_speaking", "speaking": False})
            
        threading.Thread(target=process_voice_command, args=(chunk,)).start()
        
        return jsonify({"status": "success"})
    except Exception as e:
        sse_manager.broadcast_system_log(f"PC Mic Voice Processing Failed: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@audio_bp.route('/api/soundmcu/stop', methods=['POST'])
def stop_all_audio():
    try:
        voice_queue_manager.stop()
        music_player.stop()
        
        target_ip = config.get("soundmcu", {}).get("ip")
        if target_ip and target_ip != "Unknown":
            send_tcp_command_async(target_ip, 5002, "stop", "SoundMCU")
            
        sse_manager.broadcast_system_log("모든 오디오(음성/음악/라디오) 출력이 즉시 강제 중단되었습니다.", "Sound")
        return jsonify({"status": "success", "message": "All audio stopped successfully."})
    except Exception as e:
        sse_manager.broadcast_system_log(f"오디오 즉각 정지 처리 중 오류 발생: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@audio_bp.route('/api/music/list', methods=['GET'])
def get_music_list():
    try:
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        music_dir = os.path.join(base_dir, "MusicData")
        os.makedirs(music_dir, exist_ok=True)
        files = [f for f in os.listdir(music_dir) if f.lower().endswith(('.wav', '.mp3', '.flac', '.ogg'))]
        return jsonify({"status": "success", "files": files})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@audio_bp.route('/api/music/play', methods=['POST'])
def play_specific_music():
    try:
        data = request.json
        filename = data.get("filename", "")
        voice_queue_manager.stop()
        
        if filename:
            status = music_player.get_status()
            if not status.get("is_loaded") or music_player.filename != filename:
                music_player.stop()
                time.sleep(0.05)
                
                if getattr(sys, 'frozen', False):
                    base_dir = os.path.dirname(sys.executable)
                else:
                    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
                music_file = os.path.join(base_dir, "MusicData", filename)
                
                if not os.path.exists(music_file):
                    return jsonify({"status": "error", "message": f"File not found: {filename}"}), 404
                    
                sse_manager.broadcast_system_log(f"음악 파일 즉시 교체 로딩 중: {filename}", "Sound")
                music_player.load(filename, music_file)
                
        if music_player.play():
            sse_manager.broadcast_system_log(f"음악 재생 시작", "Sound")
            return jsonify({"status": "success", "message": "Playback started"})
        return jsonify({"status": "error", "message": "Failed to start playback"}), 400
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@audio_bp.route('/api/music/pause', methods=['POST'])
def pause_music():
    music_player.pause()
    return jsonify({"status": "success"})

@audio_bp.route('/api/music/stop', methods=['POST'])
def stop_music():
    music_player.stop()
    return jsonify({"status": "success"})

@audio_bp.route('/api/music/seek', methods=['POST'])
def seek_music():
    try:
        data = request.json
        progress = float(data.get("progress", 0.0))
        music_player.seek(progress)
        return jsonify({"status": "success"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400

@audio_bp.route('/api/music/eq', methods=['POST'])
def update_music_eq():
    try:
        data = request.json
        music_player.set_eq(
            bass_db=data.get('bass', 0),
            mid_db=data.get('mid', 0),
            treble_db=data.get('treble', 0)
        )
        return jsonify({"status": "success"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400
