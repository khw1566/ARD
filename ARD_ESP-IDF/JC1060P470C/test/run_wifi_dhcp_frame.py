"""Exercise the bounded frame parser on the host; never open a device port."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
support = here.parents[1] / "shared" / "test" / "host_crt"
include = here.parent / "components" / "ard_net" / "include"
clang_bin = Path(os.environ.get(
    "ARD_TEST_CLANG_BIN",
    r"C:\Program Files\Unity\Hub\Editor\6000.5.6f1\Editor\Data\PlaybackEngines\WebGLSupport\BuildTools\Emscripten\llvm"))

with tempfile.TemporaryDirectory(prefix="ard-dhcp-frame-") as folder:
    build = Path(folder)
    objects = []
    for source in (here / "test_wifi_dhcp_frame.c", support / "host_entry.c"):
        obj = build / (source.stem + ".obj")
        subprocess.run([
            str(clang_bin / "clang.exe"), "--target=x86_64-pc-windows-msvc",
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
            "-fno-stack-protector", "-I" + str(support), "-I" + str(include),
            "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([
        str(clang_bin / "lld.exe"), "-flavor", "link", "/dll", "/noentry",
        "/machine:x64", "/def:" + str(support / "msvcrt.def"),
        "/implib:" + str(build / "msvcrt.lib"),
        "/out:" + str(build / "unused.dll")], cwd=build, check=True)
    exe = build / "test_wifi_dhcp_frame.exe"
    subprocess.run([
        str(clang_bin / "lld.exe"), "-flavor", "link", "/nodefaultlib",
        "/subsystem:console", "/entry:host_entry", "/machine:x64",
        "/out:" + str(exe), *objects, str(build / "msvcrt.lib")], check=True)
    subprocess.run([str(exe)], check=True)
