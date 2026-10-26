"""Validate the actual SD audio package. Python only; never invokes a build."""
import array
import argparse
import hashlib
import json
import math
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main(sd_root=ROOT / 'sdcard'):
    lines = json.loads((ROOT / 'tools/visitor_voice_lines.json').read_text(encoding='utf-8'))
    manifest = json.loads((sd_root / 'voice_manifest.json').read_text(encoding='utf-8'))
    indexed = {item['path']: item for item in manifest['files']}
    assert len(lines) == 14
    expected = {f'ard/voice/{lang}/{name}.wav' for lang in ('ko', 'en') for name in lines}
    assert set(indexed) == expected
    actual = {p.relative_to(sd_root).as_posix() for p in (sd_root / 'ard/voice').glob('*/*.wav')}
    assert actual == expected
    total = 0
    longest = 0
    for relative in sorted(expected):
        item = indexed[relative]
        path = sd_root / relative
        raw = path.read_bytes()
        assert raw[:4] == b'RIFF' and int.from_bytes(raw[4:8], 'little') + 8 == len(raw), relative
        assert hashlib.sha256(raw).hexdigest() == item['sha256'], relative
        with wave.open(str(path), 'rb') as wav:
            assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) == (16000, 1, 2, 'NONE'), relative
            samples = array.array('h', wav.readframes(wav.getnframes()))
            if sys.byteorder != 'little':
                samples.byteswap()
            size = len(samples) * 2
            assert 0 < size <= 512 * 1024 and size == item['pcm_bytes'], relative
            rms = math.sqrt(sum(value * value for value in samples) / len(samples))
            assert rms > 50, f'{relative}: silent or near-silent payload ({rms=})'
            assert len(samples) / 16000 + 0.5 < 7, f'{relative}: prompt exceeds slide/echo budget'
            longest = max(longest, len(samples) / 16000)
            total += size
    print(f'PASS: {len(expected)} WAV assets; PCM mono 16-bit 16000 Hz; hashes, non-silent signal, size and slide budgets')
    print(f'PCM bytes={total}; longest={longest:.3f}s; firmware cache cap=2 x 512 KiB; no compilation performed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sd-root', type=Path, default=ROOT / 'sdcard')
    main(parser.parse_args().sd_root)
