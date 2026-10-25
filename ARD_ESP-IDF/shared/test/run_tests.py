"""Compile host unit tests using MSVC, then exercise the real Wi-Fi worker."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
shared = here.parent
workspace = shared.parent
vcvars = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat")
if not vcvars.exists():
    vcvars = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")

with tempfile.TemporaryDirectory(prefix="ard-wifi-test-") as folder:
    build = Path(folder)
    for name in ["esp_err.h", "esp_event.h", "esp_log.h", "esp_timer.h", "esp_wifi.h",
                 "nvs.h", "freertos/FreeRTOS.h", "freertos/task.h"]:
        p = build / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "wifi_test_sdk.h"\n', encoding="utf-8")
    script = build / "build.cmd"
    script.write_text(
        f'@echo off\ncall "{vcvars}" >nul || exit /b 1\n'
        f'cl /nologo /utf-8 /std:c11 /W3 /I"{build}" /I"{here}" '
        f'/I"{workspace / "HMI_MCU/components/ard_net/include"}" '
        f'"{here / "test_wifi.c"}" "{shared / "ard_setup_qr.c"}" '
        f'"{shared / "third_party/qrcodegen.c"}" /Fe:"{build / "test_wifi.exe"}"\n',
        encoding="utf-8")
    subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/c", str(script)], cwd=build, check=True)
    for case in ["slots", "migration", "invalid-migration", "visibility", "failed-new", "old-password", "timeout", "scan-error", "save-retry",
                 "manual-success", "manual-rollback", "manual-empty", "qr", "slow-dhcp", "security-config",
                 "manual-transient", "delayed-disconnect", "missing-disconnect",
                 "manual-retry-success", "manual-lost-ip", "boot-quick-rescan", "boot-quick-limit",
                 "boot-retry", "paused"]:
        subprocess.run([str(build / "test_wifi.exe"), case], cwd=build, check=True)
    try:
        import cv2
    except ImportError:
        print("QR image decoding skipped: OpenCV unavailable")
    else:
        decoder = cv2.QRCodeDetector()
        for i, expected in enumerate(["WIFI:T:nopass;S:ard-hmi-123456;P:;;", "http://192.168.4.1/network"]):
            text, _, _ = decoder.detectAndDecode(cv2.imread(str(build / f"qr-{i}.pgm")))
            assert text == expected, (i, text)
        print("PASS QR image decoding (Wi-Fi credentials and setup URL)")
