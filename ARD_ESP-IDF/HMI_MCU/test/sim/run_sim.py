"""Run the real ESP32 HMI renderer in QEMU and check its captured QR screens."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[4]
PROJECT = Path(__file__).resolve().parent


def png(path, rgb, width, height):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', type=Path, default=ROOT / 'tmp/qemu/bin/qemu-system-xtensa.exe')
    parser.add_argument('--python', type=Path, default=Path(r'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe'))
    parser.add_argument('--output', type=Path, default=ROOT / 'results/261022_HMI_sim')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = PROJECT / 'build'
    flash = build / 'qemu_flash.bin'
    subprocess.run([str(args.python), '-m', 'esptool', '--chip', 'esp32', 'merge_bin',
                    '-o', str(flash), '--fill-flash-size', '4MB', '@flash_args'], cwd=build, check=True)
    command = [str(args.qemu), '-machine', 'esp32', '-nographic', '-no-reboot', '-snapshot',
               '-drive', f'file={flash.as_posix()},if=mtd,format=raw']
    log = output / 'qemu.log'
    print('Starting ESP32 HMI rendering and Wi-Fi worker tests in QEMU', flush=True)
    with log.open('wb') as stream:
        process = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                text = log.read_bytes()
                if b'[SIM_DONE]' in text:
                    break
                if process.poll() is not None:
                    raise RuntimeError(f'QEMU exited early: {process.returncode}\n{text[-4000:]!r}')
                if b'assert failed:' in text or b'Guru Meditation Error' in text:
                    raise RuntimeError(f'Emulated assertion/panic:\n{text[-5000:]!r}')
                time.sleep(0.25)
            else:
                raise TimeoutError(f'Simulation timed out; see {log}')
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=15)
    text = log.read_bytes()
    cases = ['slots', 'migration', 'invalid-migration', 'visibility', 'failed-new', 'old-password', 'timeout', 'scan-error', 'save-retry']
    for case in cases:
        assert ('PASS ' + case).encode() in text, case
        print('PASS Wi-Fi worker:', case)
    frames = {}
    for match in re.finditer(rb'\[SIM_FRAME ([\w-]+)\]\r?\n([0-9a-f\r\n]+)\[SIM_END_FRAME\]', text):
        name = match[1].decode()
        pixels = bytes.fromhex(match[2].decode())
        assert len(pixels) == 240 * 320 * 2, (name, len(pixels))
        rgb = bytearray()
        for (value,) in struct.iter_unpack('!H', pixels):
            rgb.extend(((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
        frames[name] = rgb
        png(output / (name + '.png'), rgb, 240, 320)
    assert set(frames) == {'network', 'drive', 'wifi-qr', 'web-qr', 'returned'}, frames.keys()
    assert frames['returned'] == frames['drive'], 'Returning from setup must restore the current page'
    assert frames['network'] != frames['drive'], 'Next page touch must change the rendered scene'
    decoder = ROOT / 'tmp/hmi-sim-tools/node_modules/jsqr'
    assert decoder.exists(), 'Install jsqr@1.4.0 in tmp/hmi-sim-tools to verify QR contents'
    decode_js = """
const fs=require('node:fs'),jsQR=require(process.argv[1]);
const pixels=new Uint8ClampedArray(fs.readFileSync(process.argv[2]));
const result=jsQR(pixels,240,320,{inversionAttempts:'dontInvert'});
if(!result||result.data!==process.argv[3])throw Error('QR decode mismatch: '+(result&&result.data));
console.log('PASS decoded QR: '+result.data);
"""
    for name, expected in [('wifi-qr', 'WIFI:T:nopass;S:ard-hmi-123456;P:;;'),
                           ('web-qr', 'http://192.168.4.1/network')]:
        raw = build / (name + '.rgba')
        rgb = frames[name]
        rgba = bytearray()
        for i in range(0, len(rgb), 3):
            rgba.extend(rgb[i:i + 3]); rgba.append(255)
        raw.write_bytes(rgba)
        subprocess.run(['node', '-e', decode_js, str(decoder), str(raw), expected], check=True)
    width, height = 3 * 240 + 4 * 16, 320 + 32
    preview = bytearray(bytes((24, 32, 48)) * (width * height))
    for i, name in enumerate(['network', 'wifi-qr', 'web-qr']):
        for y in range(320):
            start = ((y + 16) * width + 16 + i * 256) * 3
            preview[start:start + 720] = frames[name][y * 720:(y + 1) * 720]
    png(output / 'preview.png', preview, width, height)
    print('PASS page navigation and return from setup')
    print('Screenshots:', output, flush=True)


if __name__ == '__main__':
    main()
