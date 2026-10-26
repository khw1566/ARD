"""One display transcript for the rendered captions and external SRT."""
from pathlib import Path
import json,re
HERE=Path(__file__).resolve().parent
timing=json.loads((HERE/'narration/timing.json').read_text(encoding='utf-8'))
def display(text):
    for spoken,written in [('드라이브 엠씨유','DriveMCU'),('피씨','PC'),('에이 아이','AI'),('에이','A'),('비 입력','B 입력'),('비만','B만'),('영이','0이'),('사 대 일','4 : 1')]:text=text.replace(spoken,written)
    return text
def split(text,start,end):
    parts=re.split(r'(?<=[.!?])\s+',text.strip());out=[]
    for part in parts:
        if len(part)>34 and ',' in part:out.extend(p.strip() for p in re.split(r'(?<=,)\s+',part))
        else:out.append(part)
    weights=[len(re.sub(r'\s','',s)) for s in out];total=sum(weights);cursor=start;result=[]
    for i,(part,weight) in enumerate(zip(out,weights)):
        finish=end if i==len(out)-1 else cursor+(end-start)*weight/total
        result.append({'start':cursor,'end':finish,'text':part});cursor=finish
    return result
cg=[]
for item in timing['scenes']:cg+=split(display(item['text']),item['narration_start'],item['narration_end'])
intro=split('안녕하세요. 울산과학대학교 캡스톤디자인 NEXUS 팀장 김형우입니다. 지금부터 기계공학의 원리를 중심으로 프로젝트를 설명드리겠습니다.',.35,12.4358333333)
data={'cg':cg,'intro':intro,'method':'Sentence/phrase intervals weighted by display characters within measured speech boundaries.'}
(HERE/'narration/cg_captions.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
print('CAPTIONS_READY',len(cg),len(intro))
