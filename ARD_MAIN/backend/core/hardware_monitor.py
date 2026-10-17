import time
import threading
import subprocess
import shutil
from collections import deque
import psutil

class HardwareMonitor:
    """
    제어 PC의 CPU, RAM, GPU, 네트워크 사용률 및 상태를 실시간 백그라운드로 수집하고,
    작업 관리자 형태의 시계열 히스토리(최근 60개 포인트)를 유지 관리하는 싱글톤 매니저.
    """
    def __init__(self, history_len=60):
        self._lock = threading.Lock()
        self.history_len = history_len

        # 시계열 히스토리 큐 (최근 60초)
        self.timestamps = deque(maxlen=history_len)
        self.cpu_history = deque(maxlen=history_len)
        self.ram_history = deque(maxlen=history_len)
        self.gpu_history = deque(maxlen=history_len)
        self.net_tx_history = deque(maxlen=history_len) # KB/s
        self.net_rx_history = deque(maxlen=history_len) # KB/s

        # 정적 하드웨어 사양 정보
        self.hardware_info = {
            "cpu_name": "Intel Core / x64 Processor",
            "cpu_cores_logical": psutil.cpu_count(logical=True) or 1,
            "cpu_cores_physical": psutil.cpu_count(logical=False) or 1,
            "ram_total_gb": round(psutil.virtual_memory().total / (1024**3), 2),
            "gpu_name": "Integrated Graphics",
            "os_name": "Windows"
        }
        self._detect_gpu_name()

        # 네트워크 속도 계산용 이전 상태
        self._last_net_time = time.time()
        net_io = psutil.net_io_counters()
        self._last_bytes_sent = net_io.bytes_sent if net_io else 0
        self._last_bytes_recv = net_io.bytes_recv if net_io else 0

        # 최신 스냅샷 캐시
        self.latest_stats = {
            "cpu_percent": 0.0,
            "cpu_freq_mhz": 0,
            "ram_percent": 0.0,
            "ram_used_gb": 0.0,
            "ram_total_gb": self.hardware_info["ram_total_gb"],
            "gpu_percent": 0.0,
            "gpu_name": self.hardware_info["gpu_name"],
            "net_tx_kbs": 0.0,
            "net_rx_kbs": 0.0,
            "net_total_sent_mb": 0.0,
            "net_total_recv_mb": 0.0,
            "net_interfaces": {}
        }

        # 백그라운드 수집 스레드 시작
        self._is_running = True
        self._worker_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._worker_thread.start()

    def _detect_gpu_name(self):
        """WMI/PowerShell로 GPU 이름 식별"""
        try:
            cmd = 'powershell -NoProfile -Command "(Get-CimInstance Win32_VideoController).Name"'
            res = subprocess.check_output(cmd, shell=True, timeout=2.0).decode('cp949', errors='ignore').strip()
            if res:
                first_gpu = res.splitlines()[0].strip()
                if first_gpu:
                    self.hardware_info["gpu_name"] = first_gpu
        except Exception:
            pass

    def _get_gpu_utilization(self):
        """
        NVIDIA GPU 또는 Windows DirectX Video Controller GPU 엔진 사용률 조회
        """
        # 1. NVIDIA smi 시도
        if shutil.which("nvidia-smi"):
            try:
                cmd = "nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits"
                res = subprocess.check_output(cmd, shell=True, timeout=0.8, stderr=subprocess.DEVNULL).decode().strip()
                if res:
                    return float(res.splitlines()[0])
            except Exception:
                pass

        # 2. Windows 내장 GPU 성능 카운터 (DirectX 3D/Video)
        try:
            cmd = 'powershell -NoProfile -Command "([System.Diagnostics.PerformanceCounterCategory]::new(\'GPU Engine\')).GetInstanceNames() | Select-Object -First 1"'
            # 경량 체크: 지원되는 경우 카운터 샘플 합산
            ps_cmd = 'powershell -NoProfile -Command "(Get-Counter \'\\GPU Engine(*)\\Utilization Percentage\' -ErrorAction SilentlyContinue).CounterSamples | Measure-Object -Property CookedValue -Sum | Select-Object -ExpandProperty Sum"'
            out = subprocess.check_output(ps_cmd, shell=True, timeout=1.0).decode().strip()
            if out:
                val = float(out)
                return min(100.0, round(val, 1))
        except Exception:
            pass

        # 대체: CPU 부하와 그래픽 렌더링 추정치 (0 ~ 5%)
        return 0.0

    def _monitor_loop(self):
        """1초마다 시스템 상태를 수집하여 큐에 적재"""
        while self._is_running:
            try:
                now = time.time()
                time_str = time.strftime("%H:%M:%S", time.localtime(now))

                # 1. CPU
                cpu_p = psutil.cpu_percent(interval=None)
                freq_info = psutil.cpu_freq()
                cpu_freq = int(freq_info.current) if freq_info else 0

                # 2. RAM
                vmem = psutil.virtual_memory()
                ram_p = vmem.percent
                ram_used_gb = round(vmem.used / (1024**3), 2)

                # 3. GPU
                gpu_p = self._get_gpu_utilization()

                # 4. Network
                net_io = psutil.net_io_counters()
                dt = max(0.1, now - self._last_net_time)
                tx_kbs = 0.0
                rx_kbs = 0.0
                if net_io:
                    tx_kbs = round(((net_io.bytes_sent - self._last_bytes_sent) / dt) / 1024.0, 1)
                    rx_kbs = round(((net_io.bytes_recv - self._last_bytes_recv) / dt) / 1024.0, 1)
                    self._last_bytes_sent = net_io.bytes_sent
                    self._last_bytes_recv = net_io.bytes_recv
                    self._last_net_time = now

                # 네트워크 인터페이스 정보
                if_dict = {}
                try:
                    for if_name, addrs in psutil.net_if_addrs().items():
                        for addr in addrs:
                            if addr.family == 2: # AF_INET
                                if not addr.address.startswith("127."):
                                    if_dict[if_name] = addr.address
                except Exception:
                    pass

                with self._lock:
                    self.timestamps.append(time_str)
                    self.cpu_history.append(cpu_p)
                    self.ram_history.append(ram_p)
                    self.gpu_history.append(gpu_p)
                    self.net_tx_history.append(max(0.0, tx_kbs))
                    self.net_rx_history.append(max(0.0, rx_kbs))

                    self.latest_stats = {
                        "cpu_percent": cpu_p,
                        "cpu_freq_mhz": cpu_freq,
                        "ram_percent": ram_p,
                        "ram_used_gb": ram_used_gb,
                        "ram_total_gb": self.hardware_info["ram_total_gb"],
                        "gpu_percent": gpu_p,
                        "gpu_name": self.hardware_info["gpu_name"],
                        "net_tx_kbs": max(0.0, tx_kbs),
                        "net_rx_kbs": max(0.0, rx_kbs),
                        "net_total_sent_mb": round((net_io.bytes_sent / (1024**2)), 1) if net_io else 0.0,
                        "net_total_recv_mb": round((net_io.bytes_recv / (1024**2)), 1) if net_io else 0.0,
                        "net_interfaces": if_dict
                    }

            except Exception as e:
                pass

            time.sleep(1.0)

    def get_hardware_status(self) -> dict:
        """현재 시계열 데이터 및 하드웨어 정보 전체 반환"""
        with self._lock:
            return {
                "hardware_info": self.hardware_info,
                "latest": self.latest_stats,
                "history": {
                    "labels": list(self.timestamps),
                    "cpu": list(self.cpu_history),
                    "ram": list(self.ram_history),
                    "gpu": list(self.gpu_history),
                    "net_tx": list(self.net_tx_history),
                    "net_rx": list(self.net_rx_history)
                }
            }

hardware_monitor = HardwareMonitor()
