"""Upload the prepared SD voices over HTTP, verifying device read-back hashes."""
import argparse
import hashlib
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def transfer(base, root=ROOT / 'sdcard', verify_only=False):
    def request(path, data=None, headers=None):
        req = urllib.request.Request(base + path, data=data, headers=headers or {})
        with urllib.request.urlopen(req, timeout=30) as response:
            return json.load(response)

    state = request('/api/voice/status')
    if state.get('protocol') != 1:
        raise RuntimeError('The device does not support the voice upload protocol')
    for _ in range(20):
        if state.get('sd_ready'):
            break
        time.sleep(1)
        state = request('/api/voice/status')
    if not state.get('sd_ready'):
        raise RuntimeError('Device SD card is not mounted; no files were uploaded')
    headers = {'X-Ard-Token': state['token']}
    manifest = json.loads((root / 'voice_manifest.json').read_text(encoding='utf-8'))
    allowed = set(json.loads((ROOT / 'tools/visitor_voice_lines.json').read_text(encoding='utf-8')))
    results = []
    for item in manifest['files']:
        relative = Path(item['path'])
        parts = relative.parts
        if len(parts) != 4 or parts[:2] != ('ard', 'voice') or parts[2] not in ('ko', 'en') or relative.stem not in allowed or relative.suffix != '.wav':
            raise ValueError('Invalid asset path in manifest')
        body = (root / relative).read_bytes()
        digest = hashlib.sha256(body).hexdigest()
        if digest != item['sha256']:
            raise ValueError(f'Local asset hash mismatch: {relative}')
        endpoint = '/api/voice/file?' + urllib.parse.urlencode({'lang': parts[2], 'name': relative.stem})
        for attempt in range(6):
            try:
                if not verify_only:
                    saved = request(endpoint, body, {**headers, 'Content-Type': 'audio/wav', 'X-SHA256': digest})
                    if saved.get('sha256') != digest or saved.get('bytes') != len(body):
                        raise RuntimeError(f'Device write verification failed: {relative}')
                actual = request(endpoint, headers=headers)
                if actual.get('sha256') != digest or actual.get('bytes') != len(body):
                    raise RuntimeError(f'Device read-back mismatch: {relative}')
                break
            except urllib.error.HTTPError as error:
                if error.code != 409 or attempt == 5:
                    raise
                time.sleep(1)
        results.append({'path': relative.as_posix(), 'bytes': len(body), 'sha256': digest, 'verified': True})
        print(f'{len(results):02d}/{len(manifest["files"]):02d} verified {relative.as_posix()}', flush=True)
    return {'host': base, 'files': results, 'verified_count': len(results), 'bytes': sum(item['bytes'] for item in results)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.0.30')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if any(c in args.host for c in '/?#@'):
        parser.error('--host must be a host name or IP with an optional port')
    result = transfer('http://' + args.host, verify_only=args.verify_only)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'Completed: {result["verified_count"]} files, {result["bytes"]} bytes verified on SD')


if __name__ == '__main__':
    main()
