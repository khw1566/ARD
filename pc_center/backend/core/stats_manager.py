import time
import threading
from datetime import datetime

class StatsManager:
    """
    서버 시작 이후 누적된 각종 패킷 통신 및 외부 API/MCP 호출 통계를 관리하는 스레드 안전 싱글톤 매니저.
    """
    def __init__(self):
        self._lock = threading.Lock()
        self.server_start_time = time.time()
        
        # MCU별 패킷 통계
        # heartbeat, control_cmd, sensor_data, error_count, last_timestamp
        self.mcu_keys = ["imagesendermcu", "hmi_mcu", "drivemcu", "controlmcu", "robot3axismcu"]
        self.mcu_stats = {
            mcu: {
                "heartbeat": 0,
                "control_cmd": 0,
                "sensor_data": 0,
                "total_packets": 0,
                "error_count": 0,
                "last_timestamp": None
            }
            for mcu in self.mcu_keys
        }
        
        # API 및 AI / MCP 동작 통계
        # gemini, whisper, tts, mcp
        self.api_stats = {
            "gemini": {"call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None},
            "whisper": {"call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None},
            "tts": {"call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None},
            "mcp": {"call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None}
        }

    def _normalize_mcu_key(self, raw_name: str) -> str:
        if not raw_name:
            return "imagesendermcu"
        s = str(raw_name).lower().replace("_", "").replace("-", "")
        if "imagesender" in s or "trans" in s:
            return "imagesendermcu"
        elif "hmi" in s or "receiv" in s:
            return "hmi_mcu"
        elif "drive" in s or "motor" in s:
            return "drivemcu"
        elif "robot3" in s or "3axis" in s:
            return "robot3axismcu"
        elif "control" in s or "sound" in s:
            return "controlmcu"
        return "imagesendermcu"

    def record_mcu_packet(self, raw_mcu_name: str, packet_type: str = "heartbeat", is_error: bool = False):
        """
        MCU 패킷 수신/발신 기록
        packet_type: 'heartbeat', 'control_cmd', 'sensor_data'
        """
        key = self._normalize_mcu_key(raw_mcu_name)
        now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with self._lock:
            if key not in self.mcu_stats:
                self.mcu_stats[key] = {
                    "heartbeat": 0, "control_cmd": 0, "sensor_data": 0,
                    "total_packets": 0, "error_count": 0, "last_timestamp": None
                }
            entry = self.mcu_stats[key]
            if packet_type in entry:
                entry[packet_type] += 1
            entry["total_packets"] += 1
            if is_error:
                entry["error_count"] += 1
            entry["last_timestamp"] = now_str

    def record_api_call(self, api_name: str, success: bool = True):
        """
        외부 API / MCP 호출 결과 기록
        api_name: 'gemini', 'whisper', 'tts', 'mcp'
        """
        api_name = str(api_name).lower().strip()
        now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with self._lock:
            if api_name not in self.api_stats:
                self.api_stats[api_name] = {"call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None}
            stat = self.api_stats[api_name]
            stat["call_count"] += 1
            if success:
                stat["success_count"] += 1
            else:
                stat["fail_count"] += 1
            stat["last_timestamp"] = now_str

    def reset_stats(self):
        """통계 초기화"""
        with self._lock:
            self.server_start_time = time.time()
            for k in self.mcu_stats:
                self.mcu_stats[k] = {
                    "heartbeat": 0, "control_cmd": 0, "sensor_data": 0,
                    "total_packets": 0, "error_count": 0, "last_timestamp": None
                }
            for k in self.api_stats:
                self.api_stats[k] = {
                    "call_count": 0, "success_count": 0, "fail_count": 0, "last_timestamp": None
                }

    def get_summary(self) -> dict:
        """
        통계 집계 및 성공률(%) 계산 후 반환
        """
        with self._lock:
            now = time.time()
            uptime_seconds = int(now - self.server_start_time)
            
            hours = uptime_seconds // 3600
            minutes = (uptime_seconds % 3600) // 60
            seconds = uptime_seconds % 60
            uptime_formatted = f"{hours:02d}:{minutes:02d}:{seconds:02d}"
            
            # MCU별 성공률 및 총합 산출
            mcu_result = {}
            total_mcu_packets = 0
            total_mcu_errors = 0
            
            name_mapping = {
                "imagesendermcu": "영상 송신 보드 (ImageSenderMCU)",
                "hmi_mcu": "HMI UI 보드 (HMI_MCU)",
                "drivemcu": "모터 구동 보드 (DriveMCU)",
                "controlmcu": "제어 메인 보드 (ControlMCU)",
                "robot3axismcu": "3축 로봇 보드 (Robot3AxisMCU)"
            }
            
            for key, data in self.mcu_stats.items():
                total = data["total_packets"]
                err = data["error_count"]
                succ = max(0, total - err)
                rate = round((succ / total * 100.0), 1) if total > 0 else 100.0
                
                mcu_result[key] = {
                    "name": name_mapping.get(key, key),
                    "heartbeat": data["heartbeat"],
                    "control_cmd": data["control_cmd"],
                    "sensor_data": data["sensor_data"],
                    "total_packets": total,
                    "error_count": err,
                    "success_rate": rate,
                    "last_timestamp": data["last_timestamp"] or "-"
                }
                total_mcu_packets += total
                total_mcu_errors += err
                
            overall_mcu_rate = round(((total_mcu_packets - total_mcu_errors) / total_mcu_packets * 100.0), 1) if total_mcu_packets > 0 else 100.0

            # API 통계 포맷팅
            api_result = {}
            total_api_calls = 0
            total_api_success = 0
            
            api_display_names = {
                "gemini": "Gemini AI 시각/대화 분석",
                "whisper": "Whisper STT 음성 인식",
                "tts": "Edge/OpenAI/Azure TTS 음성 합성",
                "mcp": "MCP 도구(외부 AI 제어) 호출"
            }
            
            for key, data in self.api_stats.items():
                calls = data["call_count"]
                succ = data["success_count"]
                rate = round((succ / calls * 100.0), 1) if calls > 0 else 100.0
                
                api_result[key] = {
                    "name": api_display_names.get(key, key),
                    "call_count": calls,
                    "success_count": succ,
                    "fail_count": data["fail_count"],
                    "success_rate": rate,
                    "last_timestamp": data["last_timestamp"] or "-"
                }
                total_api_calls += calls
                total_api_success += succ
                
            overall_api_rate = round((total_api_success / total_api_calls * 100.0), 1) if total_api_calls > 0 else 100.0

            return {
                "server_start_time": datetime.fromtimestamp(self.server_start_time).strftime("%Y-%m-%d %H:%M:%S"),
                "uptime_seconds": uptime_seconds,
                "uptime_formatted": uptime_formatted,
                "mcu_stats": mcu_result,
                "total_mcu_packets": total_mcu_packets,
                "overall_mcu_success_rate": overall_mcu_rate,
                "api_stats": api_result,
                "total_api_calls": total_api_calls,
                "overall_api_success_rate": overall_api_rate
            }

stats_manager = StatsManager()
