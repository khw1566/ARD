import os
import sys
import re
import asyncio
import time
import subprocess
import edge_tts
from openai import OpenAI
import soundfile as sf
import numpy as np
from scipy import signal
import requests
from gtts import gTTS

from config.config_manager import config
from network.sse_manager import sse_manager
from audio.voice_queue import voice_queue_manager

SAMPLE_RATE = 16000

def clean_text_for_tts(text):
    if not text: return ""
    msg = re.sub(r'[*#_~>`\[\]]', '', text)
    msg = "".join(c for c in msg if c <= "\uFFFF")
    msg = " ".join(msg.split())
    return msg.strip()

def tts_to_pcm_and_send(text, priority=1):
    try:
        if not text:
            return None
            
        detected_style = "friendly"
        cleaned_msg = text.strip()
        match = re.match(r"^\[([a-zA-Z]+)\]", cleaned_msg)
        if match:
            tag = match.group(1).lower()
            if tag in ["cheerful", "empathetic", "sad", "angry", "excited", "friendly", "whispering"]:
                detected_style = tag
                cleaned_msg = cleaned_msg.replace(match.group(0), "", 1).strip()
        
        cleaned_msg = clean_text_for_tts(cleaned_msg)
        if not cleaned_msg:
            return None
            
        engine_name = config["soundmcu"]["tts_engine"]
        voice_id = config["soundmcu"]["tts_voice"]
        pitch_val = config["soundmcu"]["tts_pitch"]
        rate_val = config["soundmcu"]["tts_rate"]
        effect = config["soundmcu"]["tts_effect"]
        
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            
        audio_dir = os.path.join(base_dir, "static", "audio")
        os.makedirs(audio_dir, exist_ok=True)
        
        ext = "wav" if engine_name in ["Pyttsx3", "SAPI", "Sherpa", "Azure-TTS"] else "mp3"
        audio_file = os.path.join(audio_dir, f"response.{ext}")
        
        success = False
        
        if engine_name == "Edge-TTS":
            if pitch_val == 0 and rate_val == 0:
                async def generate_edge():
                    communicate = edge_tts.Communicate(cleaned_msg, voice_id)
                    await communicate.save(audio_file)
                asyncio.run(generate_edge())
            else:
                pitch_str = f"{'+' if pitch_val >= 0 else ''}{pitch_val}%"
                rate_str = f"{'+' if rate_val >= 0 else ''}{rate_val}%"
                
                ssml = f"<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='ko-KR'><voice name='{voice_id}'><prosody pitch='{pitch_str}' rate='{rate_str}'>{cleaned_msg}</prosody></voice></speak>"
                
                async def generate_edge():
                    communicate = edge_tts.Communicate(ssml)
                    await communicate.save(audio_file)
                asyncio.run(generate_edge())
            success = True
            
        elif engine_name == "OpenAI":
            api_key = config["soundmcu"]["openai_api_key"]
            if not api_key:
                sse_manager.broadcast_system_log("OpenAI API Key가 설정되지 않았습니다.", "Error")
                return None
            client = OpenAI(api_key=api_key)
            speed = 1.0 + (rate_val / 100.0)
            speed = max(0.25, min(4.0, speed))
            
            response = client.audio.speech.create(
                model="tts-1",
                voice=voice_id,
                input=cleaned_msg,
                speed=speed
            )
            response.stream_to_file(audio_file)
            success = True
            
        elif engine_name == "Azure-TTS":
            api_key = config["soundmcu"].get("azure_api_key", "")
            region = config["soundmcu"].get("azure_region", "koreacentral")
            if not api_key:
                sse_manager.broadcast_system_log("Azure API Key가 설정되지 않았습니다.", "Error")
                return None
            
            pitch_str = f"{'+' if pitch_val >= 0 else ''}{pitch_val}%"
            rate_str = f"{'+' if rate_val >= 0 else ''}{rate_val}%"
            style_degree = config["soundmcu"].get("tts_style_degree", 1.5)
            
            ssml = (
                f"<speak version='1.0' xml:lang='ko-KR' xmlns='http://www.w3.org/2001/10/synthesis' xmlns:mstts='https://www.w3.org/2001/mstts'>"
                f"<voice name='{voice_id}'>"
                f"<mstts:express-as style='{detected_style}' styledegree='{style_degree}'>"
                f"<prosody pitch='{pitch_str}' rate='{rate_str}'>{cleaned_msg}</prosody>"
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
            
            try:
                res = requests.post(url, headers=headers, data=ssml.encode('utf-8'), timeout=10)
                if res.status_code == 200:
                    with open(audio_file, "wb") as f:
                        f.write(res.content)
                    success = True
                else:
                    error_msg = f"Azure TTS HTTP Error: {res.status_code} - {res.text}"
                    print(error_msg)
                    sse_manager.broadcast_system_log(error_msg, "Error")
            except Exception as e:
                print(f"Azure TTS Request Exception: {e}")
                sse_manager.broadcast_system_log(f"Azure TTS Request Exception: {e}", "Error")
                
        if not success:
            tts = gTTS(text=cleaned_msg, lang='ko')
            audio_file = os.path.join(audio_dir, "response.mp3")
            tts.save(audio_file)
            success = True

        # Convert to PCM 16kHz
        try:
            if audio_file.endswith(".mp3"):
                raise ValueError("MP3 must be converted via ffmpeg")
            data, fs = sf.read(audio_file)
        except Exception:
            wav_path = audio_file + "_temp.wav"
            subprocess.run(["ffmpeg", "-y", "-i", audio_file, "-acodec", "pcm_s16le", "-ar", "16000", "-ac", "1", wav_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if os.path.exists(wav_path):
                data, fs = sf.read(wav_path)
                try: os.remove(wav_path)
                except: pass
            else:
                sse_manager.broadcast_system_log(f"TTS 오디오 디코딩 실패 (FFmpeg 변환 오류)", "Error")
                return None
        
        if effect == "Robot":
            t = np.arange(len(data)) / fs
            carrier = np.sin(2 * np.pi * 60 * t)
            if len(data.shape) > 1:
                for c in range(data.shape[1]): data[:, c] *= carrier
            else:
                data *= carrier
            sse_manager.broadcast_system_log("Robot 효과 적용됨", "Sound")

        if len(data.shape) > 1: data = data[:, 0]
        
        if fs != SAMPLE_RATE:
            num_samples = int(len(data) * SAMPLE_RATE / fs)
            data = signal.resample(data, num_samples)
        
        pcm_data = (data * 32767).astype(np.int16)
        
        silence_padding = np.zeros(1600, dtype=np.int16)
        pcm_data = np.concatenate([silence_padding, pcm_data])
        
        if success and config.get("soundmcu", {}).get("ip") and config.get("soundmcu", {}).get("ip") != "Unknown":
            voice_queue_manager.add_pcm(pcm_data, priority=priority)
        
        if os.path.exists(audio_file):
            filename = os.path.basename(audio_file)
            final_url = f"/outputs/audio/{filename}?t={int(time.time())}"
            sse_manager.broadcast_system_log(f"Browser Audio URL 준비 완료: {final_url}", "Sound")
            return final_url
        else:
            sse_manager.broadcast_system_log("TTS 파일이 생성되지 않았습니다.", "Error")
            return None
        
    except Exception as e:
        sse_manager.broadcast_system_log(f"TTS Process Error: {e}", "Error")
        return None
