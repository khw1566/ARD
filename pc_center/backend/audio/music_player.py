import threading
import time
import socket
import numpy as np
import soundfile as sf

from config.config_manager import config

AUDIO_UDP_PORT = 5001
SAMPLE_RATE = 16000

def resample_linear(data, orig_sr, target_sr):
    if orig_sr == target_sr:
        return data
    ratio = float(target_sr) / orig_sr
    num_samples = int(len(data) * ratio)
    indices = np.linspace(0, len(data) - 1, num_samples)
    idx_floor = np.floor(indices).astype(np.int32)
    idx_ceil = np.minimum(idx_floor + 1, len(data) - 1)
    weight = indices - idx_floor
    resampled = (1 - weight) * data[idx_floor] + weight * data[idx_ceil]
    return resampled

class MusicPlayer:
    """Manages playing local and downloaded music files."""
    def __init__(self):
        self.lock = threading.Lock()
        self.original_pcm_float = None
        self.equalized_pcm = None
        self.current_sample_idx = 0
        self.is_playing = False
        self.is_loaded = False
        self.filename = ""
        self.duration_seconds = 0.0
        self.bass_db = 0.0
        self.mid_db = 0.0
        self.treble_db = 0.0
        self.playback_thread = None

    def load(self, filename, music_file):
        with self.lock:
            self.filename = filename
            audio_data, fs = sf.read(music_file)
            if len(audio_data.shape) > 1:
                audio_data = audio_data[:, 0] # Mono conversion
            
            # Use high-speed linear resampler to 16kHz
            self.original_pcm_float = resample_linear(audio_data, fs, SAMPLE_RATE)
            self.duration_seconds = len(self.original_pcm_float) / SAMPLE_RATE
            self.current_sample_idx = 0
            self.is_loaded = True
            
            # Apply equalizer
            self._apply_eq_no_lock()

    def _apply_eq_no_lock(self):
        if self.original_pcm_float is None:
            return
            
        n_samples = len(self.original_pcm_float)
        
        # If the EQ is flat (0dB for all), skip FFT for speed
        if self.bass_db == 0.0 and self.mid_db == 0.0 and self.treble_db == 0.0:
            self.equalized_pcm = (np.clip(self.original_pcm_float, -1.0, 1.0) * 32767).astype(np.int16)
            return
            
        # Fast FFT EQ
        fft_data = np.fft.rfft(self.original_pcm_float)
        freqs = np.fft.rfftfreq(n_samples, d=1/SAMPLE_RATE)
        
        # Calculate linear scale gains
        bass_gain = 10 ** (self.bass_db / 20)
        mid_gain = 10 ** (self.mid_db / 20)
        treble_gain = 10 ** (self.treble_db / 20)
        
        # Apply gains to masks
        bass_mask = freqs < 250
        mid_mask = (freqs >= 250) & (freqs < 4000)
        treble_mask = freqs >= 4000
        
        fft_data[bass_mask] *= bass_gain
        fft_data[mid_mask] *= mid_gain
        fft_data[treble_mask] *= treble_gain
        
        eq_float = np.fft.irfft(fft_data, n=n_samples)
        self.equalized_pcm = (np.clip(eq_float, -1.0, 1.0) * 32767).astype(np.int16)

    def set_eq(self, bass_db, mid_db, treble_db):
        with self.lock:
            self.bass_db = float(bass_db)
            self.mid_db = float(mid_db)
            self.treble_db = float(treble_db)
            if self.is_loaded:
                self._apply_eq_no_lock()

    def play(self):
        with self.lock:
            if not self.is_loaded:
                return False
                
            # Note: Radio Streamer stop logic should be handled by caller to avoid circular deps
            if self.is_playing:
                return True
            self.is_playing = True
            
            self.playback_thread = threading.Thread(target=self._playback_loop, daemon=True)
            self.playback_thread.start()
            return True

    def pause(self):
        with self.lock:
            self.is_playing = False

    def stop(self):
        with self.lock:
            self.is_playing = False
            self.current_sample_idx = 0

    def seek(self, progress_ratio):
        with self.lock:
            if not self.is_loaded or self.equalized_pcm is None:
                return
            progress_ratio = max(0.0, min(1.0, float(progress_ratio)))
            self.current_sample_idx = int(progress_ratio * len(self.equalized_pcm))

    def get_status(self):
        with self.lock:
            if not self.is_loaded:
                return {
                    "is_loaded": False,
                    "is_playing": False,
                    "filename": "",
                    "duration": 0,
                    "position": 0,
                    "progress": 0.0,
                    "bass": self.bass_db,
                    "mid": self.mid_db,
                    "treble": self.treble_db
                }
            
            pos_sec = self.current_sample_idx / SAMPLE_RATE
            progress = self.current_sample_idx / len(self.equalized_pcm) if self.equalized_pcm is not None and len(self.equalized_pcm) > 0 else 0.0
            return {
                "is_loaded": True,
                "is_playing": self.is_playing,
                "filename": self.filename,
                "duration": self.duration_seconds,
                "position": pos_sec,
                "progress": progress,
                "bass": self.bass_db,
                "mid": self.mid_db,
                "treble": self.treble_db
            }

    def _playback_loop(self):
        target_ip = config.get("soundmcu", {}).get("ip")
        if not target_ip or target_ip == "Unknown":
            with self.lock:
                self.is_playing = False
            return
            
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        chunk_samples = 512 # 32ms pacing at 16kHz
        chunk_bytes = chunk_samples * 2
        
        packet_duration = 0.032
        start_time = time.perf_counter()
        loop_count = 0
        
        while True:
            with self.lock:
                if not self.is_playing or not self.is_loaded or self.equalized_pcm is None:
                    break
                    
                if self.current_sample_idx >= len(self.equalized_pcm):
                    self.is_playing = False
                    self.current_sample_idx = 0
                    break
                    
                start_idx = self.current_sample_idx
                end_idx = min(start_idx + chunk_samples, len(self.equalized_pcm))
                chunk_data = self.equalized_pcm[start_idx:end_idx]
                vol_factor = config.get("soundmcu", {}).get("volume", 12) / 21.0
                chunk = (chunk_data * vol_factor).astype(np.int16).tobytes()
                self.current_sample_idx = end_idx
                
            if len(chunk) < chunk_bytes:
                chunk += bytes(chunk_bytes - len(chunk))
                
            sock.sendto(chunk, (target_ip, AUDIO_UDP_PORT))
            
            loop_count += 1
            target_time = start_time + loop_count * packet_duration
            sleep_dur = target_time - time.perf_counter()
            if sleep_dur > 0:
                time.sleep(sleep_dur)
            else:
                start_time = time.perf_counter()
                loop_count = 0

# Create a singleton instance
music_player = MusicPlayer()
