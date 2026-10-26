"""Use the user's existing local fine-tuned XTTS engine; no training or upload."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
import time

HERE = Path(__file__).resolve().parent


def sections(path):
    text = path.read_text(encoding='utf-8')
    pattern = r'\[C(\d+) / (\d+):(\d+)–(\d+):(\d+) / ([^\]]+)\]\s*([^\[]+)'
    result = []
    for match in re.finditer(pattern, text):
        number, sm, ss, em, es, title, words = match.groups()
        result.append({'id': int(number), 'start': int(sm)*60+int(ss),
                       'end': int(em)*60+int(es), 'title': title,
                       'text': ' '.join(words.split())})
    if len(result) != 15:
        raise ValueError(f'Expected 15 narration sections, found {len(result)}')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mytts', type=Path, default=Path(r'C:\Users\work\Desktop\myTTS'))
    parser.add_argument('--script', type=Path, default=HERE/'CG_개인TTS대본_v4.txt')
    parser.add_argument('--speed', type=float, default=1.0)
    parser.add_argument('--temperature', type=float, default=.65)
    parser.add_argument('--only', type=int, nargs='*')
    args = parser.parse_args()
    output = HERE/'narration'
    output.mkdir(exist_ok=True)
    os.environ.setdefault('HF_HOME', str(HERE/'render_jobs'/'cache'/'huggingface'))
    os.environ.setdefault('TORCH_HOME', str(HERE/'render_jobs'/'cache'/'torch'))
    os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
    sys.path.insert(0, str(args.mytts))
    import numpy as np
    import soundfile as sf
    import torch
    from tts_engine import VoiceTTS, OUTPUT_SR
    torch.manual_seed(726)
    records = sections(args.script)
    print(json.dumps({'status':'loading local fine-tuned model','scenes':len(records)},ensure_ascii=False),flush=True)
    engine = VoiceTTS()
    model_id = str(engine.checkpoint.relative_to(args.mytts))
    print(json.dumps({'status':'model ready','device':engine.device,'checkpoint':model_id},ensure_ascii=False),flush=True)
    for item in records:
        key = hashlib.sha256((item['text']+model_id+str(args.speed)+str(args.temperature)).encode()).hexdigest()
        wav_path = output/f"C{item['id']:02d}.wav"
        metadata = output/f"C{item['id']:02d}.json"
        if args.only and item['id'] not in args.only:
            previous=json.loads(metadata.read_text(encoding='utf-8'))
            if previous['text'] != item['text']:
                raise ValueError('A skipped clip has different text')
            item.update(previous)
            continue
        if wav_path.exists() and metadata.exists() and json.loads(metadata.read_text(encoding='utf-8')).get('key') == key:
            item.update(json.loads(metadata.read_text(encoding='utf-8')))
            print(json.dumps({'cached':item['id'],'seconds':item['seconds']},ensure_ascii=False),flush=True)
            continue
        started = time.monotonic()
        torch.manual_seed(726+item['id'])
        wav = engine.synthesize(item['text'],language='ko',temperature=args.temperature,speed=args.speed)
        if not np.isfinite(wav).all() or len(wav) < OUTPUT_SR:
            raise ValueError(f"Invalid audio in C{item['id']:02d}")
        peak = float(np.max(np.abs(wav)))
        if peak > .98:
            wav = wav*(.98/peak)
        sf.write(str(wav_path),wav,OUTPUT_SR,subtype='PCM_16')
        item.update({'file':wav_path.name,'seconds':len(wav)/OUTPUT_SR,'sample_rate':OUTPUT_SR,
                     'peak_before_limit':peak,'key':key,'checkpoint':model_id,'speed':args.speed,'temperature':args.temperature})
        metadata.write_text(json.dumps(item,ensure_ascii=False,indent=2),encoding='utf-8')
        (output/'manifest.partial.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'scene':item['id'],'seconds':round(item['seconds'],3),'slot':item['end']-item['start'],
                          'generation_seconds':round(time.monotonic()-started,1)},ensure_ascii=False),flush=True)
    (output/'manifest.json').write_text(json.dumps({'engine':'myTTS fine-tuned XTTS-v2',
        'checkpoint':model_id,'sample_rate':OUTPUT_SR,'scenes':records},ensure_ascii=False,indent=2),encoding='utf-8')
    print('NARRATION_READY',flush=True)


if __name__ == '__main__':
    main()
