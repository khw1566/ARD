import threading
import queue
import time
import socket
import numpy as np

# from app.py globals
AUDIO_UDP_PORT = 5001
SAMPLE_RATE = 16000

class VoiceQueueManager:
    """Manages playing voice responses sequentially to SoundMCU."""
    def __init__(self):
        self.queue = queue.PriorityQueue() # (priority, timestamp, data)
        self.is_playing = False
        self.lock = threading.Lock()
        self.thread = None
        self.stop_event = threading.Event()
        
    def add_pcm(self, pcm_data: np.ndarray, priority=1):
        with self.lock:
            # Drop low priority if queue is full
            if self.queue.qsize() > 5 and priority > 0:
                return
            self.queue.put((priority, time.time(), pcm_data))
            
            if not self.is_playing:
                self.is_playing = True
                self.stop_event.clear()
                self.thread = threading.Thread(target=self._play_loop, daemon=True)
                self.thread.start()
                
    def stop(self):
        self.stop_event.set()
        with self.lock:
            self.is_playing = False
            while not self.queue.empty():
                try: 
                    self.queue.get_nowait()
                except queue.Empty: 
                    break

    def _play_loop(self):
        from config.config_manager import config
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        
        while not self.stop_event.is_set():
            try:
                priority, timestamp, pcm_data = self.queue.get(timeout=1.0)
            except queue.Empty:
                with self.lock:
                    self.is_playing = False
                break
                
            target_ip = config.get("soundmcu", {}).get("ip")
            if not target_ip or target_ip == "Unknown":
                continue
                
            chunk_size = 512 # 32ms of 16kHz Int16 Mono
            
            for i in range(0, len(pcm_data), chunk_size // 2):
                if self.stop_event.is_set():
                    break
                chunk = pcm_data[i:i + chunk_size // 2].tobytes()
                try:
                    sock.sendto(chunk, (target_ip, AUDIO_UDP_PORT))
                except Exception as e:
                    print(f"UDP audio send error: {e}")
                    break # Usually network error, break chunk loop
                time.sleep(0.016)
        sock.close()

voice_queue_manager = VoiceQueueManager()
