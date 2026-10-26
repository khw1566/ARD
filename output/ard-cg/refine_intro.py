"""Compare clear proper-name delivery candidates using the same private voice."""
from pathlib import Path
import json,os,sys
HERE=Path(__file__).resolve().parent;OUT=HERE/'live'/'intro_candidates';OUT.mkdir(exist_ok=True)
MYTTS=Path(r'C:\Users\work\Desktop\myTTS')
os.environ['PYTHONDONTWRITEBYTECODE']='1';os.environ['HF_HOME']=str(HERE/'render_jobs/cache/huggingface')
sys.path.insert(0,str(MYTTS))
import torch,soundfile as sf,numpy as np
from tts_engine import VoiceTTS,OUTPUT_SR
engine=VoiceTTS();records=[]
for i,(seed,temperature) in enumerate([(1831,.5),(1842,.5),(1858,.55)]):
    text='안녕하세요. 울산 과학 대학교, 캡스톤 디자인 넥서스 팀장, 김 형우입니다. 지금부터 기계 공학의 원리를 중심으로 프로젝트를 설명드리겠습니다.'
    path=OUT/f'intro_{i+1}.wav'
    torch.manual_seed(seed);wav=engine.synthesize(text,language='ko',temperature=temperature,speed=1.)
    peak=float(np.max(np.abs(wav)));wav=wav*min(1.,.98/max(peak,.01));sf.write(path,wav,OUTPUT_SR,subtype='PCM_16')
    item={'id':f'intro_{i+1}','file':str(path),'text':text,'seconds':len(wav)/OUTPUT_SR,'seed':seed,'temperature':temperature}
    records.append(item);print(json.dumps(item,ensure_ascii=False),flush=True)
(OUT/'candidates.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
