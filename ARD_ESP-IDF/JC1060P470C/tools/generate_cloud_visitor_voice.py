"""Generate cloud TTS media without copying credentials into the output or firmware."""
import argparse
import base64
import concurrent.futures
import hashlib
import html
import io
import json
import time
import urllib.error
import urllib.request
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_KEYS = Path(r'C:\Users\work\Desktop\Mini WebServer\mini-WEB (M3-ESP-IDF_ver)\sd_private\.private\api_keys.json')


def google(keys, text, voice, rate=1.08):
    body = {'input': {'text': text}, 'voice': {'languageCode': voice[:5], 'name': voice},
            'audioConfig': {'audioEncoding': 'LINEAR16', 'sampleRateHertz': 16000, 'speakingRate': rate}}
    req = urllib.request.Request('https://texttospeech.googleapis.com/v1/text:synthesize',
        data=json.dumps(body).encode(), headers={'Content-Type': 'application/json', 'X-Goog-Api-Key': keys['google_tts']})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=45) as response:
                return base64.b64decode(json.load(response)['audioContent'], validate=True)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt == 2:
                raise RuntimeError(f'Google TTS HTTP {error.code}; credentials omitted') from None
            time.sleep(2 ** attempt)


def azure(keys, text, voice):
    ssml = (f'<speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis" xml:lang="{voice[:5]}">'
            f'<voice name="{voice}"><prosody rate="+5%">{html.escape(text)}</prosody></voice></speak>')
    region = keys.get('azure_region', 'koreacentral')
    req = urllib.request.Request(f'https://{region}.tts.speech.microsoft.com/cognitiveservices/v1',
        data=ssml.encode(), headers={'Content-Type': 'application/ssml+xml',
        'Ocp-Apim-Subscription-Key': keys['azure_speech'],
        'X-Microsoft-OutputFormat': 'riff-16khz-16bit-mono-pcm', 'User-Agent': 'ARD-Visitor-Voice'})
    try:
        with urllib.request.urlopen(req, timeout=45) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        raise RuntimeError(f'Azure TTS HTTP {error.code}; credentials omitted') from None


def validate(raw):
    with wave.open(io.BytesIO(raw), 'rb') as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) != (16000, 1, 2, 'NONE'):
            raise RuntimeError('TTS did not return mono PCM16 / 16000 Hz')
        frames = wav.getnframes()
    if frames <= 0 or frames * 2 > 512 * 1024:
        raise RuntimeError('Voice exceeds firmware PCM size limit')
    if frames / 16000 + 0.5 >= 7:
        raise RuntimeError('Voice exceeds screensaver slide duration budget')
    return frames


def produce(keys, provider, voice, text, path):
    metadata = path.with_suffix('.json')
    job = {'provider': provider, 'voice': voice, 'text': text, 'rate': 1.08 if provider == 'google' else 1.05}
    if path.exists() and metadata.exists() and json.loads(metadata.read_text(encoding='utf-8')).get('job') == job:
        raw = path.read_bytes()
    else:
        raw = google(keys, text, voice) if provider == 'google' else azure(keys, text, voice)
        validate(raw)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
    frames = validate(raw)
    item = {'job': job, 'seconds': round(frames / 16000, 3), 'pcm_bytes': frames * 2,
            'sha256': hashlib.sha256(raw).hexdigest()}
    metadata.write_text(json.dumps(item, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return item


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keys', type=Path, default=DEFAULT_KEYS)
    parser.add_argument('--output', type=Path, default=ROOT.parents[1] / 'output/jc1060-cloud-voice')
    args = parser.parse_args()
    keys = json.loads(args.keys.read_text(encoding='utf-8-sig'))
    lines = json.loads((ROOT / 'tools/visitor_voice_lines.json').read_text(encoding='utf-8'))
    pack = args.output / 'sdcard'
    jobs = [(lang, name, text[lang]) for lang in ('ko', 'en') for name, text in lines.items()]

    def generate(job):
        lang, name, text = job
        voice = ('ko-KR' if lang == 'ko' else 'en-US') + '-Chirp3-HD-Aoede'
        relative = f'ard/voice/{lang}/{name}.wav'
        info = produce(keys, 'google', voice, text, pack / relative)
        print(f'Google {lang}/{name}: {info["seconds"]:.3f}s', flush=True)
        return {'path': relative, 'provider': 'Google Cloud Text-to-Speech', 'voice': voice,
                'seconds': info['seconds'], 'pcm_bytes': info['pcm_bytes'], 'sha256': info['sha256']}

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        files = list(pool.map(generate, jobs))
    manifest = {'format': 'pcm_s16le', 'rate': 16000, 'channels': 1,
        'synthesis': 'Google Cloud Chirp 3 HD Aoede, speakingRate 1.08; server output 16000 Hz',
        'files': sorted(files, key=lambda item: item['path'])}
    (pack / 'voice_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for provider, voice, stem in [('azure', 'ko-KR-SunHiNeural', 'azure_sunhi_welcome'),
                                  ('azure', 'ko-KR-HyunsuMultilingualNeural', 'azure_hyunsu_welcome'),
                                  ('google', 'ko-KR-Chirp3-HD-Kore', 'google_kore_welcome')]:
        info = produce(keys, provider, voice, lines['welcome']['ko'], args.output / 'comparison' / (stem + '.wav'))
        print(f'Comparison {voice}: {info["seconds"]:.3f}s', flush=True)
    print('Generated 28 Google Cloud HD clips and 3 cloud comparison clips; no credentials written to output.')


if __name__ == '__main__':
    main()
