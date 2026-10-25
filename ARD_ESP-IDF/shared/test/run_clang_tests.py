"""Run the Wi-Fi worker tests on Windows without a Visual Studio installation.

Uses an installed clang with the x86-64 target and the Windows CRT import
library. This builds no target firmware and opens no serial ports.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
here = Path(__file__).resolve().parent
clang_bin = Path(os.environ.get(
    "ARD_TEST_CLANG_BIN",
    r"C:\Program Files\Unity\Hub\Editor\6000.5.6f1\Editor\Data\PlaybackEngines\WebGLSupport\BuildTools\Emscripten\llvm"))
# The minimal CRT declarations are kept with this runner so the test does not
# depend on old compiled objects or an installed Windows SDK.
support = here / "host_crt"
cases = ["slots", "migration", "invalid-migration", "visibility", "failed-new",
         "old-password", "timeout", "scan-error", "save-retry", "manual-success",
         "manual-rollback", "manual-empty", "qr", "slow-dhcp", "security-config",
         "manual-transient", "delayed-disconnect", "missing-disconnect",
         "manual-retry-success", "manual-lost-ip"]

with tempfile.TemporaryDirectory(prefix="ard-wifi-clang-") as folder:
    build = Path(folder)
    for name in ["esp_err.h", "esp_event.h", "esp_log.h", "esp_timer.h", "esp_wifi.h",
                 "nvs.h", "freertos/FreeRTOS.h", "freertos/task.h"]:
        p = build / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "wifi_test_sdk.h"\n', encoding="utf-8")
    sources = [here / "test_wifi.c", here.parent / "ard_setup_qr.c",
               here.parent / "third_party/qrcodegen.c", support / "host_entry.c"]
    objects = []
    for source in sources:
        obj = build / (source.stem + ".obj")
        subprocess.run([str(clang_bin / "clang.exe"), "--target=x86_64-pc-windows-msvc",
                        "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-fno-builtin", "-fno-stack-protector", "-I" + str(support),
                        "-I" + str(build), "-I" + str(here),
                        "-I" + str(root / "ARD_ESP-IDF/JC1060P470C/components/ard_net/include"),
                        "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([str(clang_bin / "lld.exe"), "-flavor", "link", "/dll", "/noentry",
                    "/machine:x64", "/def:" + str(support / "msvcrt.def"),
                    "/implib:" + str(build / "msvcrt.lib"),
                    "/out:" + str(build / "unused.dll")], cwd=build, check=True)
    exe = build / "test_wifi.exe"
    subprocess.run([str(clang_bin / "lld.exe"), "-flavor", "link", "/nodefaultlib",
                    "/subsystem:console", "/entry:host_entry", "/machine:x64",
                    "/out:" + str(exe), *objects, str(build / "msvcrt.lib")], check=True)
    for case in cases:
        subprocess.run([str(exe), case], cwd=build, check=True)
