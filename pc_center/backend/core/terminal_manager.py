"""Terminal Manager for handling CLI commands from internal Web Terminal.
Supports system version inspect, server restart/shutdown, hardware status, stats query, ping, and raw commands.
"""
import os
import sys
import platform
import subprocess
import threading
import time
import socket
from typing import Dict, Any

class TerminalManager:
    """Class responsible for executing and formatting commands within the internal terminal."""

    def __init__(self, stats_mgr=None, hw_monitor=None):
        self.stats_mgr = stats_mgr
        self.hw_monitor = hw_monitor
        self.python_path = sys.executable

    def execute(self, cmd_line: str) -> Dict[str, Any]:
        """Parse and execute a command string."""
        raw = cmd_line.strip()
        if not raw:
            return {"output": "", "status": "ok"}

        parts = raw.split()
        command = parts[0].lower()
        args = parts[1:]

        if command == "help":
            return {"output": self._cmd_help(), "status": "ok"}
        elif command in ("version", "ver"):
            return {"output": self._cmd_version(), "status": "ok"}
        elif command == "clear":
            return {"output": "__CLEAR__", "status": "ok"}
        elif command in ("hw", "hardware"):
            return {"output": self._cmd_hardware(), "status": "ok"}
        elif command == "stats":
            return {"output": self._cmd_stats(args), "status": "ok"}
        elif command == "ping":
            return {"output": self._cmd_ping(args), "status": "ok"}
        elif command == "server":
            return self._cmd_server(args)
        elif command == "exec":
            return {"output": self._cmd_exec(" ".join(args)), "status": "ok"}
        else:
            return {
                "output": f"알 수 없는 명령어: '{command}'. 'help'를 입력하여 사용 가능한 명령어를 확인하십시오.",
                "status": "error"
            }

    def _cmd_help(self) -> str:
        lines = [
            "======================= ARD 내부 터미널 명령어 목록 =======================",
            "- help                     : 사용 가능한 명령어 안내 출력",
            "- version / ver            : Python, OS, PlatformIO 및 주요 패키지 버전 확인",
            "- server restart           : 백엔드 서버 프로세스 안전 재시작",
            "- server stop              : 백엔드 서버 프로세스 종료",
            "- hw / hardware            : 현재 제어 PC 하드웨어(CPU/RAM/GPU/Network) 상세 상태",
            "- stats                    : 누적 통신 패킷 및 API 동작 통계 요약",
            "- stats reset              : 누적 통계 데이터 초기화",
            "- ping <host/IP>           : 지정된 호스트에 대한 네트워크 소켓/ICMP 연결 진단",
            "- exec <shell command>     : 시스템 쉘 명령어 실행 (주의하여 사용)",
            "- clear                    : 터미널 콘솔 화면 초기화",
            "==========================================================================="
        ]
        return "\n".join(lines)

    def _cmd_version(self) -> str:
        lines = [
            "======================== 시스템 소프트웨어 버전 정보 ========================",
            f"- 운영체제 (OS)      : {platform.system()} {platform.release()} ({platform.version()})",
            f"- 파이썬 경로 (Python): {self.python_path}",
            f"- 파이썬 버전        : {platform.python_version()} ({platform.architecture()[0]})",
        ]
        
        # Check Flask version
        try:
            import flask
            lines.append(f"- Flask 프레임워크   : {flask.__version__}")
        except Exception:
            lines.append("- Flask 프레임워크   : 미설치 또는 확인 불가")

        # Check PlatformIO version
        try:
            res = subprocess.run(["pio", "--version"], capture_output=True, text=True, timeout=3)
            pio_ver = res.stdout.strip() if res.returncode == 0 else "미설치"
            lines.append(f"- PlatformIO CLI     : {pio_ver}")
        except Exception:
            lines.append("- PlatformIO CLI     : 미설치 또는 실행 실패")

        # Check PyTorch / GPU support
        try:
            import torch
            lines.append(f"- PyTorch 버전       : {torch.__version__} (CUDA 사용가능: {torch.cuda.is_available()})")
        except Exception:
            lines.append("- PyTorch 버전       : 미설치")

        # Check Git
        try:
            res = subprocess.run(["git", "--version"], capture_output=True, text=True, timeout=2)
            git_ver = res.stdout.strip() if res.returncode == 0 else "미설치"
            lines.append(f"- Git 형상관리       : {git_ver}")
        except Exception:
            lines.append("- Git 형상관리       : 미설치")

        lines.append("===========================================================================")
        return "\n".join(lines)

    def _cmd_hardware(self) -> str:
        if not self.hw_monitor:
            return "하드웨어 모니터링 모듈이 연결되어 있지 않습니다."
        data = self.hw_monitor.get_hardware_status()
        hw_info = data.get("hardware_info", {})
        latest = data.get("latest", {})
        lines = [
            "======================== 제어 PC 하드웨어 상태 리포트 ========================",
            f"- CPU 모델           : {hw_info.get('cpu_name', 'N/A')}",
            f"- CPU 코어 수        : 물리 {hw_info.get('cpu_cores_physical', 0)}개 / 논리 {hw_info.get('cpu_cores_logical', 0)}개",
            f"- CPU 사용률         : {latest.get('cpu_percent', 0)} %",
            f"- CPU 동작 주파수    : {latest.get('cpu_freq_mhz', 0)} MHz",
            f"- RAM 사용률         : {latest.get('ram_percent', 0)} % ({latest.get('ram_used_gb', 0)} GB / {latest.get('ram_total_gb', 0)} GB)",
            f"- GPU 모델           : {latest.get('gpu_name', 'N/A')}",
            f"- GPU 사용률         : {latest.get('gpu_percent', 0)} %",
            f"- 네트워크 송신 속도 : {latest.get('net_tx_kbs', 0)} KB/s (누적: {latest.get('net_total_sent_mb', 0)} MB)",
            f"- 네트워크 수신 속도 : {latest.get('net_rx_kbs', 0)} KB/s (누적: {latest.get('net_total_recv_mb', 0)} MB)",
            "==========================================================================="
        ]
        return "\n".join(lines)

    def _cmd_stats(self, args) -> str:
        if not self.stats_mgr:
            return "통계 관리 모듈이 연결되어 있지 않습니다."
        if args and args[0].lower() == "reset":
            self.stats_mgr.reset_stats()
            return "모든 누적 패킷 및 API 통계가 성공적으로 초기화되었습니다."
        
        summary = self.stats_mgr.get_summary()
        mcu_stats = summary.get("mcu_stats", {})
        api_stats = summary.get("api_stats", {})

        lines = [
            "======================== 시스템 통신 및 API 통계 ========================",
            f"- 서버 시작 시각     : {summary.get('server_start_time', '-')}",
            f"- 가동 시간 (Uptime) : {summary.get('uptime_formatted', '0초')}",
            f"- 총 MCU 패킷 수     : {summary.get('total_mcu_packets', 0):,} 건 (성공률: {summary.get('overall_mcu_success_rate', 100.0)}%)",
            f"- 총 API 호출 횟수   : {summary.get('total_api_calls', 0):,} 건 (성공률: {summary.get('overall_api_success_rate', 100.0)}%)",
            "---------------------------- [MCU별 통신 현황] ----------------------------",
        ]
        for mcu, s in mcu_stats.items():
            lines.append(f"  * {mcu:<14}: 전체 {s.get('total_packets',0):>6} | 하트비트 {s.get('heartbeat',0):>5} | 제어 {s.get('control_cmd',0):>5} | 센서 {s.get('sensor_data',0):>5} | 에러 {s.get('error_count',0):>3} | 성공률 {s.get('success_rate', 100.0):>5.1f}%")
        
        lines.append("---------------------------- [외부 AI / MCP API 현황] ---------------------")
        for api, s in api_stats.items():
            lines.append(f"  * {api:<14}: 총 {s.get('call_count',0):>5}회 | 성공 {s.get('success_count',0):>5} | 실패 {s.get('fail_count',0):>4} | 성공률 {s.get('success_rate',100.0):>5.1f}%")
        lines.append("===========================================================================")
        return "\n".join(lines)

    def _cmd_ping(self, args) -> str:
        if not args:
            return "사용법: ping <호스트 또는 IP 주소> [포트]"
        host = args[0]
        port = int(args[1]) if len(args) > 1 and args[1].isdigit() else None

        if port:
            # TCP socket ping
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)
            start = time.time()
            try:
                s.connect((host, port))
                latency = (time.time() - start) * 1000.0
                s.close()
                return f"[TCP PING 성공] {host}:{port} 응답 시간: {latency:.1f}ms"
            except Exception as ex:
                return f"[TCP PING 실패] {host}:{port} 연결 불가: {ex}"
        else:
            # OS ping
            try:
                cmd = ["ping", "-n", "2", host] if platform.system() == "Windows" else ["ping", "-c", "2", host]
                res = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
                return res.stdout.strip()
            except subprocess.TimeoutExpired:
                return f"[PING 타임아웃] {host}로부터 응답이 없습니다."
            except Exception as ex:
                return f"[PING 오류] {ex}"

    def _cmd_server(self, args) -> Dict[str, Any]:
        if not args:
            return {"output": "사용법: server restart | server stop", "status": "error"}
        action = args[0].lower()
        if action == "restart":
            threading.Thread(target=self._delayed_restart, daemon=False).start()
            return {
                "output": "[서버 공지] 백엔드 서버를 1.5초 후 안전하게 재기동합니다. 잠시 후 페이지를 새로고침하십시오...",
                "status": "restarting"
            }
        elif action in ("stop", "shutdown"):
            threading.Thread(target=self._delayed_shutdown, daemon=False).start()
            return {
                "output": "[서버 공지] 백엔드 서버를 1.5초 후 완전히 종료합니다.",
                "status": "stopping"
            }
        else:
            return {"output": f"지원하지 않는 서버 작업: {action}", "status": "error"}

    def _cmd_exec(self, shell_cmd: str) -> str:
        if not shell_cmd:
            return "사용법: exec <명령어>"
        try:
            res = subprocess.run(
                shell_cmd,
                shell=True,
                capture_output=True,
                text=True,
                timeout=8,
                cwd=os.path.dirname(os.path.abspath(__file__))
            )
            out = res.stdout if res.returncode == 0 else (res.stderr or res.stdout)
            return out.strip() if out.strip() else "[명령어 실행 완료 (출력 없음)]"
        except subprocess.TimeoutExpired:
            return "[실행 실패] 명령어 실행 제한 시간(8초)을 초과하였습니다."
        except Exception as ex:
            return f"[실행 오류] {ex}"

    def _delayed_restart(self):
        time.sleep(1.5)
        # Spawn new python process
        backend_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        app_file = os.path.join(backend_dir, "app.py")
        subprocess.Popen([self.python_path, app_file], cwd=backend_dir)
        os._exit(0)

    def _delayed_shutdown(self):
        time.sleep(1.5)
        os._exit(0)
