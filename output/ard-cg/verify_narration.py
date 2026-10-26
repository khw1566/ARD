"""Local ASR aid for pronunciation review; no audio/model transmission."""
from pathlib import Path
import difflib
import json
import os
import sys
import argparse

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'render_jobs'/'asr_packages'))
os.environ['HF_HUB_OFFLINE']='1'
from faster_whisper import WhisperModel
cache=Path.home()/'.cache'/'huggingface'/'hub'/'models--Systran--faster-whisper-small'/'snapshots'
model_path=next(path for path in cache.iterdir() if (path/'model.bin').exists())
model=WhisperModel(str(model_path),device='cpu',compute_type='int8',cpu_threads=8,local_files_only=True)
parser=argparse.ArgumentParser();parser.add_argument('--only',type=int,nargs='*');args=parser.parse_args()
manifest=json.loads((HERE/'narration'/'manifest.json').read_text(encoding='utf-8'))
review=HERE/'final'/'narration_asr_review.json'
previous=json.loads(review.read_text(encoding='utf-8')) if args.only and review.exists() else []
result=[item for item in previous if item['id'] not in (args.only or [])]
for item in manifest['scenes']:
    if args.only and item['id'] not in args.only:continue
    parts,info=model.transcribe(str(HERE/'narration'/f"C{item['id']:02d}_mix.wav"),language='ko',beam_size=3,condition_on_previous_text=False,vad_filter=False)
    recognized=' '.join(part.text.strip() for part in parts)
    normalize=lambda text:''.join(c for c in text if c.isalnum())
    similarity=difflib.SequenceMatcher(None,normalize(item['text']),normalize(recognized)).ratio()
    record={'id':item['id'],'expected':item['text'],'recognized':recognized,'text_similarity':similarity}
    result.append(record)
    review.write_text(json.dumps(sorted(result,key=lambda item:item['id']),ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(record,ensure_ascii=False),flush=True)
print('LOCAL_ASR_COMPLETE',flush=True)
