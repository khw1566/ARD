from pathlib import Path
import difflib,json,os,shutil,sys
HERE=Path(__file__).resolve().parent;OUT=HERE/'live'
sys.path.insert(0,str(HERE/'render_jobs'/'asr_packages'));os.environ['HF_HUB_OFFLINE']='1'
from faster_whisper import WhisperModel
cache=Path.home()/'.cache/huggingface/hub/models--Systran--faster-whisper-small/snapshots'
model=WhisperModel(str(next(p for p in cache.iterdir() if (p/'model.bin').exists())),device='cpu',compute_type='int8',cpu_threads=8,local_files_only=True)
records=json.loads((OUT/'intro_candidates/candidates.json').read_text(encoding='utf-8'));normalize=lambda s:''.join(c for c in s if c.isalnum())
expected='안녕하세요. 울산과학대학교 캡스톤디자인 NEXUS 팀장 김형우입니다. 지금부터 기계공학의 원리를 중심으로 프로젝트를 설명드리겠습니다.'
for item in records:
    segments,info=model.transcribe(item['file'],language='ko',beam_size=5,condition_on_previous_text=False)
    item['recognized']=' '.join(s.text.strip() for s in segments);norm=normalize(item['recognized'])
    item['school_exact']='울산과학대학교' in norm;item['name_exact']='김형우' in norm
    item['similarity']=difflib.SequenceMatcher(None,normalize(item['text']),norm).ratio()
    item['score']=3*item['name_exact']+2*item['school_exact']+item['similarity']
    print(json.dumps(item,ensure_ascii=False),flush=True)
selected=max(records,key=lambda item:item['score'])
shutil.copyfile(selected['file'],OUT/'intro.wav')
record={'id':'intro','title':'발표자 소개','text':selected['text'],'display':expected,'file':'intro.wav','seconds':selected['seconds'],
        'sample_rate':24000,'selected_candidate':selected['id'],'seed':selected['seed'],'temperature':selected['temperature'],
        'asr_school_exact':selected['school_exact'],'asr_name_exact':selected['name_exact']}
(OUT/'intro.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
manifest=json.loads((OUT/'additional_narration.json').read_text(encoding='utf-8'));manifest[0]=record
(OUT/'additional_narration.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'intro_candidate_review.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
print('INTRO_SELECTED '+selected['id'],flush=True)
