import time
import os
import numpy as np
import soundfile as sf
import whisper
from openai import OpenAI

from config.config_manager import config
from network.sse_manager import sse_manager
from core.state_manager import state_manager
from audio.tts_engine import tts_to_pcm_and_send

whisper_model = None
def get_whisper_model():
    global whisper_model
    if whisper_model is None:
        try:
            print("Loading local Whisper model (tiny)...")
            whisper_model = whisper.load_model("tiny")
            print("Whisper model loaded.")
        except Exception as e:
            print(f"Error loading whisper: {e}")
            return None
    return whisper_model

def process_voice_command(buffer):
    """Processes recorded PCM audio buffer using STT, then passes to AI, and plays TTS response."""
    # We need to import the AI part dynamically to avoid circular dependencies if it hasn't been extracted yet
    # Or assuming ai/llm_manager.py will be there, we can import it
    from ai.llm_manager import get_gemini_conversational_response
    
    try:
        t_start = time.perf_counter()
        turn_id = f"t_{int(t_start * 1000)}"
        
        stt_engine = config["soundmcu"].get("stt_engine", "Whisper-Local")
        text = ""
        
        if stt_engine == "OpenAI-API":
            api_key = config["soundmcu"].get("openai_api_key", "")
            if not api_key:
                sse_manager.broadcast_system_log("OpenAI API Key가 설정되지 않았습니다. Whisper-Local로 전환합니다.", "Warning")
                stt_engine = "Whisper-Local"
            else:
                client = OpenAI(api_key=api_key)
                temp_wav = "stt_temp.wav"
                sf.write(temp_wav, buffer, 16000)
                with open(temp_wav, "rb") as audio_file:
                    transcription = client.audio.transcriptions.create(
                        model="whisper-1", 
                        file=audio_file,
                        language="ko"
                    )
                text = transcription.text.strip()
                os.remove(temp_wav)

        if not text: # Whisper-Local
            model = get_whisper_model()
            if model is None: return
            audio_f32 = buffer.astype(np.float32) / 32768.0
            result = model.transcribe(audio_f32, language="ko")
            text = result["text"].strip()
            
        t_stt_end = time.perf_counter()
        if not text:
            state_manager.device_stats["SoundMCU"]["status"] = "Idle"
            sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
            return
            
        stt_ms = int((t_stt_end - t_start) * 1000)
        user_payload = {
            "type": "update",
            "turn_id": turn_id,
            "analysis": {"type": "voice", "role": "User", "text": text},
            "timings": {"stt": stt_ms}
        }
        sse_manager.broadcast_event(user_payload)
        sse_manager.broadcast_system_log(f"Recognized ({stt_engine}): {text}", "STT")

        # 2. AI Phase
        sse_manager.broadcast_system_log("AI 분석 요청 중 (Gemini)...", "AI")
        ai_response = get_gemini_conversational_response(text)
        t_ai_end = time.perf_counter()
        ai_ms = int((t_ai_end - t_stt_end) * 1000)
        
        sse_manager.broadcast_system_log("TTS 음성 합성 시작...", "Sound")
        audio_url = tts_to_pcm_and_send(ai_response)
        
        ai_payload = {
            "type": "update",
            "turn_id": turn_id,
            "analysis": {"type": "voice", "role": "AI", "text": ai_response, "audio_url": audio_url},
            "timings": {"stt": stt_ms, "ai": ai_ms}
        }
        sse_manager.broadcast_event(ai_payload)
        sse_manager.broadcast_system_log(f"AI 응답 완료: {ai_response[:30]}...", "Gemini")

        # 3. TTS Phase
        state_manager.device_stats["SoundMCU"]["status"] = "Speaking"
        sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
        
        t_tts_end = time.perf_counter()
        tts_ms = int((t_tts_end - t_ai_end) * 1000)
        total_ms = int((t_tts_end - t_start) * 1000)
        
        final_payload = {
            "type": "update",
            "turn_id": turn_id,
            "timings": {
                "stt": stt_ms,
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
        sse_manager.broadcast_system_log(f"Voice Processing Error: {e}", "Error")
        state_manager.device_stats["SoundMCU"]["status"] = "Idle"
        sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
