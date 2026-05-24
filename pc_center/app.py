import os
import sys
import time
import socket
import threading
import webbrowser
from threading import Timer
import subprocess

try:
    import psutil
except ImportError:
    psutil = None

import config
from config import CONFIG, load_ai_persona, update_gemini_config, get_local_ip
import network_manager
from network_manager import (
    udp_receive_thread, audio_receive_thread, discovery_broadcast_thread,
    monitor_device_status_loop, get_whisper_model
)
from gui_server import app

def ensure_single_instance():
    try:
        lock_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        lock_sock.bind(('127.0.0.1', 49999))
        return lock_sock
    except socket.error:
        print("[Startup] Another instance is already running. Exiting.")
        os._exit(0)

def kill_port_process(port):
    if psutil is None:
        try:
            print(f"[Startup] psutil not found. Using netstat/taskkill fallback for port {port}...")
            output = subprocess.check_output(f'netstat -ano | findstr :{port}', shell=True).decode('cp949', errors='ignore')
            lines = [line.strip() for line in output.split('\n') if line.strip()]
            pids = set()
            for line in lines:
                parts = line.split()
                if len(parts) >= 5 and 'LISTENING' in line:
                    pids.add(parts[-1])
            
            for pid in pids:
                if pid != '0':
                    print(f"[Startup] Found PID {pid} using port {port}. Terminating...")
                    subprocess.run(f'taskkill /F /PID {pid}', shell=True, capture_output=True)
            return len(pids) > 0
        except Exception as e:
            print(f"[Startup] Port cleanup fallback failed: {e}")
            return False

    for proc in psutil.process_iter(['pid', 'name']):
        try:
            for conn in proc.connections(kind='inet'):
                if conn.laddr.port == port:
                    print(f"[Startup] Found process {proc.info['name']} (PID: {proc.info['pid']}) using port {port}. Terminating...")
                    proc.terminate()
                    proc.wait(timeout=3)
                    print(f"[Startup] Successfully terminated process on port {port}.")
                    return True
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.TimeoutExpired):
            pass
    return False

def server_heartbeat():
    """HMI 보드로 PC Center(서버)가 살아있음을 지속적 브로드캐스트."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while True:
        hmi_ip = CONFIG["hmi_mcu_ip"]
        if hmi_ip:
            try:
                sock.sendto("hb=1".encode(), (hmi_ip, 5003))
            except Exception:
                pass
        time.sleep(1.0)

def preload_whisper():
    # 1단계 환경
    config._current_startup_idx = 1
    time.sleep(0.5)
    
    # 2단계 설정
    config._current_startup_idx = 2
    time.sleep(0.5)
    
    # 3단계 Whisper AI 로드
    config._current_startup_idx = 3
    try:
        get_whisper_model()
    except Exception as e:
        print(f"[Whisper Warmer] Preload Failed: {e}")
        
    # 4단계 네트워크
    config._current_startup_idx = 4
    time.sleep(0.5)
    
    # 5단계 레디 (인덱스는 0-based이므로 마지막 항목은 4)
    config._current_startup_idx = 4
    print("[Whisper Warmer] System ready!")

if __name__ == "__main__":
    # 1. 단일 인스턴스 보장
    instance_lock = ensure_single_instance()
    
    # 2. 포트 충돌 방지 및 프로세스 정리
    pc_port = int(os.getenv("PC_PORT", 5000))
    kill_port_process(pc_port)
    
    print("[Startup] Initializing PC Center Modules...")
    
    # 3. 설정 갱신
    load_ai_persona()
    update_gemini_config()
    
    # 4. 백그라운드 스레드 가동
    threads = [
        threading.Thread(target=udp_receive_thread, daemon=True, name="UDPReceive"),
        threading.Thread(target=audio_receive_thread, daemon=True, name="AudioReceive"),
        threading.Thread(target=discovery_broadcast_thread, daemon=True, name="DiscoveryBroadcast"),
        threading.Thread(target=monitor_device_status_loop, daemon=True, name="DeviceMonitor"),
        threading.Thread(target=server_heartbeat, daemon=True, name="ServerHeartbeat"),
        threading.Thread(target=preload_whisper, daemon=True, name="WhisperPreload")
    ]
    
    for t in threads:
        t.start()
        print(f"[Startup] Started {t.name} thread.")

    # 5. 브라우저 자동 실행
    server_ip = get_local_ip()
    url = f"http://{server_ip}:{pc_port}"
    print(f"[Startup] Serving PC Center at {url}")
    Timer(1.5, lambda: webbrowser.open(url)).start()
    
    # 6. Flask 서버 가동 (메인 스레드 점유)
    try:
        app.run(host="0.0.0.0", port=pc_port, debug=False, use_reloader=False)
    except KeyboardInterrupt:
        print("[Shutdown] Interrupted by user. Exiting.")
        os._exit(0)
