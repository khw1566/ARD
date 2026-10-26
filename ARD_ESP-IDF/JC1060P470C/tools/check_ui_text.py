"""Check that every English key in main/ui_text.cpp still matches a string in the UI sources.

A key that no longer matches is silently left untranslated, so run this after
changing UI text:  python tools/check_ui_text.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['ui.cpp', 'ui_main.cpp', 'ui_settings.cpp', 'ui_screensaver.cpp', 'c6_update.cpp']
MACROS = {'DEG': '\\xC2\\xB0', 'CONFIG_ARD_DEFAULT_SERVER': '<SERVER>'}


def literals(text):
    """C string literals with adjacent literals and known macros joined; comments and chars skipped."""
    out, parts, i, n = [], None, 0, len(text)

    def flush():
        nonlocal parts
        if parts is not None:
            out.append(''.join(parts))
        parts = None

    while i < n:
        c = text[i]
        if text.startswith('//', i):
            end = text.find('\n', i)
            i = n if end < 0 else end
        elif text.startswith('/*', i):
            i = text.index('*/', i) + 2
        elif c in '"\'':
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == '\\' else 1
            if c == '"':
                parts = (parts or []) + [text[i + 1:j]]
            else:
                flush()
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            m = re.match(r'\b(' + '|'.join(MACROS) + r')\b', text[i:])
            if m and parts is not None:
                parts.append(MACROS[m.group(1)])
                i += m.end()
                continue
            flush()
            i += 1
    flush()
    return out


def main():
    used = set()
    for name in SOURCES:
        used.update(literals((ROOT / 'main' / name).read_text(encoding='utf-8')))
    table = literals((ROOT / 'main/ui_text.cpp').read_text(encoding='utf-8'))
    keys = [s for s in table if s.isascii() and s not in ('nvs.h', 'sdkconfig.h', 'ard-ui', 'lang', 'ui_text.h', 'C')]
    missing = [k for k in keys if k not in used]
    for key in missing:
        print('NO MATCH:', key)
    print(f'{len(keys)} keys checked, {len(missing)} without a matching UI string')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
