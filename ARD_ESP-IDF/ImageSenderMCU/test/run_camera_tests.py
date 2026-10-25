"""Run real camera.c with mocked hardware using the existing Windows host CRT."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CRT = HERE.parents[1] / "shared/test/host_crt"
CLANG = Path(os.environ.get("ARD_TEST_CLANG_BIN",
    "C:/Program Files/Unity/Hub/Editor/6000.5.6f1/Editor/Data/PlaybackEngines/WebGLSupport/BuildTools/Emscripten/llvm"))
with tempfile.TemporaryDirectory(prefix="ard-camera-host-") as folder:
    build = Path(folder)
    for name in ["esp_err.h", "esp_log.h", "esp_psram.h", "esp_camera.h",
                 "freertos/FreeRTOS.h", "freertos/task.h"]:
        header = build / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "fake_camera_sdk.h"\n', encoding="utf-8")
    objects = []
    for source in [HERE / "test_camera.c", CRT / "host_entry.c"]:
        obj = build / (source.stem + ".obj")
        subprocess.run([str(CLANG / "clang.exe"), "--target=x86_64-pc-windows-msvc",
                        "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                        "-Wno-unused-variable", "-fno-builtin", "-fno-stack-protector",
                        "-I" + str(CRT), "-I" + str(build), "-I" + str(HERE),
                        "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([str(CLANG / "lld.exe"), "-flavor", "link", "/dll", "/noentry",
                    "/machine:x64", "/def:" + str(CRT / "msvcrt.def"),
                    "/implib:" + str(build / "msvcrt.lib"), "/out:" + str(build / "unused.dll")],
                   cwd=build, check=True)
    exe = build / "camera_test.exe"
    subprocess.run([str(CLANG / "lld.exe"), "-flavor", "link", "/nodefaultlib",
                    "/subsystem:console", "/entry:host_entry", "/machine:x64",
                    "/out:" + str(exe), *objects, str(build / "msvcrt.lib")], check=True)
    for case in ["psram", "dram", "ownership", "retry", "timeout", "init-failure", "apply"]:
        subprocess.run([str(exe), case], check=True)
