"""Synthesize the speaker introduction and observed live-demo narration locally."""
from pathlib import Path
import json
import os
import sys
import time

HERE=Path(__file__).resolve().parent
MYTTS=Path(r'C:\Users\work\Desktop\myTTS')
OUT=HERE/'live';OUT.mkdir(exist_ok=True)
os.environ['HF_HOME']=str(HERE/'render_jobs'/'cache'/'huggingface')
os.environ['TORCH_HOME']=str(HERE/'render_jobs'/'cache'/'torch')
os.environ['PYTHONDONTWRITEBYTECODE']='1'
sys.path.insert(0,str(MYTTS))
import numpy as np
import soundfile as sf
import torch
from tts_engine import VoiceTTS,OUTPUT_SR

records=[
 {'id':'intro','title':'발표자 소개','text':'안녕하세요. 울산과학대학교 캡스톤 디자인, 넥서스 팀장 김형우입니다. 지금부터 기계공학적 원리를 중심으로 프로젝트를 설명드리겠습니다.','display':'안녕하세요. 울산과학대학교 캡스톤디자인 NEXUS 팀장 김형우입니다. 지금부터 기계공학적 원리를 중심으로 프로젝트를 설명드리겠습니다.'},
 {'id':'live_1','start':0.8,'end':11.8,'title':'버튼 입력과 플레이트 동작','text':'이제 실제 장치의 움직임을 보겠습니다. 터치 화면에서 명령을 입력하고, 플레이트의 기울임과 복귀 동작을 확인합니다.'},
 {'id':'live_2','start':13.2,'end':25.8,'title':'실물 기어와 자세 전환','text':'두 모터의 입력은 베벨 기어를 거쳐 플레이트의 자세로 전달됩니다. 앞에서 설명한 기어 구조가 실제 움직임으로 이어지는 모습입니다.'},
 {'id':'live_3','start':28.2,'end':41.7,'title':'터치 조이스틱 조작','text':'이어서 터치 조이스틱으로 입력을 연속적으로 바꿉니다. 화면 조작에 따라 플레이트가 움직이는 모습을 확인할 수 있습니다.'},
]
print('ADDITIONAL_VOICE_LOADING',flush=True)
engine=VoiceTTS()
for i,item in enumerate(records):
    path=OUT/(item['id']+'.wav');meta=path.with_suffix('.json')
    if path.exists() and meta.exists() and json.loads(meta.read_text(encoding='utf-8'))['text']==item['text']:
        item.update(json.loads(meta.read_text(encoding='utf-8')));continue
    torch.manual_seed(1826+i)
    started=time.monotonic();wav=engine.synthesize(item['text'],language='ko',temperature=.55,speed=1.)
    if not np.isfinite(wav).all():raise ValueError(item['id'])
    peak=float(np.max(np.abs(wav)))
    if peak>.98:wav=wav*.98/peak
    sf.write(path,wav,OUTPUT_SR,subtype='PCM_16')
    item.update(seconds=len(wav)/OUTPUT_SR,sample_rate=OUTPUT_SR,checkpoint=str(engine.checkpoint.relative_to(MYTTS)),file=path.name)
    meta.write_text(json.dumps(item,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'id':item['id'],'duration':round(item['seconds'],3),'generation_seconds':round(time.monotonic()-started,2)},ensure_ascii=False),flush=True)
(OUT/'additional_narration.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
print('ADDITIONAL_VOICE_READY',flush=True)
