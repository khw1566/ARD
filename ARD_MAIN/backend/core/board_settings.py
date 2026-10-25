"""Persistent PC-side board selection. No firmware or credentials live here."""
import ipaddress
import json
import os
from pathlib import Path
import threading


DEVICE_NAMES = {
    "imagesendermcu": "ImageSenderMCU", "hmi_mcu": "HMI_MCU",
    "drivemcu": "DriveMCU", "controlmcu": "ControlMCU",
    "robot3axismcu": "Robot3AxisMCU",
}
DEFAULTS = {"robot3axis_enabled": True, "control_terminal": "legacy",
            "voice_device": "controlmcu", "jc1060_ip": ""}


def device_key(name):
    name = str(name).lower().replace("_", "").replace("-", "")
    if "jc1060" in name: return "hmi_mcu"
    if "imagesender" in name or "transmitter" in name: return "imagesendermcu"
    if "hmi" in name or "receiver" in name: return "hmi_mcu"
    if "robot3" in name or "3axis" in name: return "robot3axismcu"
    if "drive" in name or "motor" in name: return "drivemcu"
    if "control" in name or "sound" in name or "voice" in name: return "controlmcu"
    return None


class BoardSettings:
    def __init__(self, path):
        self.path = Path(path)
        self._lock = threading.RLock()
        self._settings = DEFAULTS.copy()
        if self.path.exists():
            try:
                self._settings = self.validate(json.loads(self.path.read_text(encoding="utf-8")))
            except (OSError, ValueError, TypeError) as exc:
                # Keep a damaged file for diagnosis; do not rewrite it during startup.
                print(f"[Boards] Cannot load {self.path.name}: {exc}; using defaults")

    def snapshot(self):
        with self._lock:
            return self._settings.copy()

    def validate(self, changes):
        if not isinstance(changes, dict):
            raise ValueError("설정은 JSON 객체여야 합니다.")
        if set(changes) - set(DEFAULTS):
            raise ValueError("지원하지 않는 보드 설정입니다.")
        candidate = self._settings.copy()
        candidate.update(changes)
        if "control_terminal" in changes and "voice_device" not in changes and changes["control_terminal"] != self._settings["control_terminal"]:
            candidate["voice_device"] = "jc1060" if changes["control_terminal"] == "jc1060" else "controlmcu"
        if type(candidate["robot3axis_enabled"]) is not bool:
            raise ValueError("3축 로봇 사용 여부는 true/false여야 합니다.")
        if candidate["control_terminal"] not in ("legacy", "jc1060"):
            raise ValueError("제어 단말은 legacy 또는 jc1060이어야 합니다.")
        if candidate["voice_device"] not in ("controlmcu", "jc1060"):
            raise ValueError("음성 보드는 controlmcu 또는 jc1060이어야 합니다.")
        value = candidate["jc1060_ip"]
        if not isinstance(value, str):
            raise ValueError("JC1060 IP는 문자열이어야 합니다.")
        value = value.strip()
        if value:
            try:
                ipaddress.IPv4Address(value)
            except ValueError:
                raise ValueError("JC1060 IPv4 주소를 확인하세요.") from None
        candidate["jc1060_ip"] = value
        return candidate

    def update(self, changes):
        with self._lock:
            candidate = self.validate(changes)
            temporary = self.path.with_suffix(".json.tmp")
            try:
                temporary.write_text(json.dumps(candidate, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                os.replace(temporary, self.path)
            finally:
                temporary.unlink(missing_ok=True)
            self._settings = candidate
            return candidate.copy()

    def enabled(self, key):
        settings = self.snapshot()
        if key == "robot3axismcu": return settings["robot3axis_enabled"]
        if key == "controlmcu":
            return settings["control_terminal"] == "legacy" or settings["voice_device"] == "controlmcu"
        return True

    def accepts(self, key, data, ip, jc_ip=""):
        settings = self.snapshot()
        if not self.enabled(key): return False
        jc_ip = settings["jc1060_ip"] or jc_ip
        tagged_jc = "jc1060" in str(data.get("board", data.get("source", ""))).lower()
        if key == "hmi_mcu":
            if settings["control_terminal"] == "jc1060":
                return (ip == jc_ip) if jc_ip else tagged_jc
            return not tagged_jc and not (jc_ip and ip == jc_ip)
        if key == "drivemcu":
            # DriveMCU remains a separate board under either terminal selection.
            # Legacy JC firmware may still announce itself as DriveMCU; ignore it.
            return not tagged_jc and not (jc_ip and ip == jc_ip)
        if key == "controlmcu" and jc_ip and ip == jc_ip:
            return settings["voice_device"] == "jc1060"
        return True

    def decorate_stats(self, summary):
        settings = self.snapshot()
        summary["hardware"] = settings
        stats = summary["mcu_stats"]
        for key in list(stats):
            if not self.enabled(key): del stats[key]
        if settings["control_terminal"] == "jc1060":
            if "hmi_mcu" in stats: stats["hmi_mcu"]["name"] = "JC1060 통합 단말"
            if "controlmcu" in stats: stats["controlmcu"]["name"] = "음성 보드 (ControlMCU)"
        total = sum(item["total_packets"] for item in stats.values())
        errors = sum(item["error_count"] for item in stats.values())
        summary["total_mcu_packets"] = total
        summary["overall_mcu_success_rate"] = round(100 * (total - errors) / total, 1) if total else 100.0
        return summary
