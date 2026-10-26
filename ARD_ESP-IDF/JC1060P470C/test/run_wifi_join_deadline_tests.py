"""Compile/run the pure join deadline policy on the PC, without firmware or USB access."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
support = here.parents[1] / "shared" / "test" / "host_crt"
clang_bin = Path(os.environ.get(
    "ARD_TEST_CLANG_BIN",
    r"C:\Program Files\Unity\Hub\Editor\6000.5.6f1\Editor\Data\PlaybackEngines\WebGLSupport\BuildTools\Emscripten\llvm"))

with tempfile.TemporaryDirectory(prefix="ard-join-deadline-") as folder:
    build = Path(folder)
    objects = []
    for source in (here / "test_wifi_join_deadline.c", support / "host_entry.c"):
        obj = build / (source.stem + ".obj")
        subprocess.run([str(clang_bin / "clang.exe"), "--target=x86_64-pc-windows-msvc",
                        "-std=c11", "-Wall", "-Wextra", "-fno-builtin",
                        "-fno-stack-protector", "-I" + str(support),
                        "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([str(clang_bin / "lld.exe"), "-flavor", "link", "/dll", "/noentry",
                    "/machine:x64", "/def:" + str(support / "msvcrt.def"),
                    "/implib:" + str(build / "msvcrt.lib"),
                    "/out:" + str(build / "unused.dll")], cwd=build, check=True)
    executable = build / "test_wifi_join_deadline.exe"
    subprocess.run([str(clang_bin / "lld.exe"), "-flavor", "link", "/nodefaultlib",
                    "/subsystem:console", "/entry:host_entry", "/machine:x64",
                    "/out:" + str(executable), *objects, str(build / "msvcrt.lib")], check=True)
    subprocess.run([str(executable)], check=True)
