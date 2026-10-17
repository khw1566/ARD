"""Build the boot-screen logo from the PC program icon.

Reads ARD_MAIN/frontend/static/app_icon_transparent.png, trims the transparent
border, scales it to SIZE px on the splash background (white) and writes
main/assets/logo.rgb565: 2-byte width + 2-byte height (little-endian), then
big-endian RGB565 pixels, ready to blit without decoding. Needs Pillow.

    python tools/gen_logo.py
"""
import pathlib
import struct

from PIL import Image

SIZE = 120
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT.parents[1] / "ARD_MAIN" / "frontend" / "static" / "app_icon_transparent.png"
OUT = ROOT / "main" / "assets" / "logo.rgb565"


def main():
    icon = Image.open(SRC).convert("RGBA")
    # The 1024 px icon carries a white matte; trim on non-white, non-transparent pixels.
    rgb = Image.new("RGB", icon.size, "white")
    rgb.paste(icon, mask=icon.getchannel("A"))
    gray = rgb.convert("L").point(lambda v: 255 if v < 245 else 0)
    icon = rgb.crop(gray.getbbox())
    icon.thumbnail((SIZE, SIZE), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (SIZE, SIZE), "white")
    canvas.paste(icon, ((SIZE - icon.width) // 2, (SIZE - icon.height) // 2))

    data = bytearray(struct.pack("<HH", SIZE, SIZE))
    raw = canvas.tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        data += struct.pack(">H", ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    OUT.write_bytes(data)
    canvas.save(OUT.with_suffix(".preview.png"))
    print(f"wrote {OUT} ({SIZE}x{SIZE}, {len(data)} bytes)")


if __name__ == "__main__":
    main()
