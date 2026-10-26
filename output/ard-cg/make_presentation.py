"""Combine a personal introduction, mechanical CG, and real stabilized demo."""
from pathlib import Path
import json,math,re,subprocess
import cv2
import numpy as np
import soundfile as sf
from mix_audio import trim_speech,srt_time

HERE=Path(__file__).resolve().parent;OUT=HERE/'live';FINAL=HERE/'final'
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe');FPS=30;SR=48000;TOTAL_FRAMES=5400
CG=FINAL/'ARD_CG_MyVoice_1080p.mp4'
selected=json.loads((OUT/'selected_stabilized.json').read_text(encoding='utf-8'))
STABLE=OUT/selected['file']
records=json.loads((OUT/'additional_narration.json').read_text(encoding='utf-8'))
intro=next(item for item in records if item['id']=='intro')
live_records=[item for item in records if item['id'].startswith('live_')]

def run(args,name):
    with (OUT/(name+'.log')).open('w',encoding='utf-8') as log:
        subprocess.run([str(FFMPEG),'-hide_banner','-y',*map(str,args)],cwd=OUT,check=True,stdout=log,stderr=subprocess.STDOUT)

def prepare_voice(item,slot=None):
    audio,sr=sf.read(OUT/item['file'],dtype='float32');audio=trim_speech(audio,sr)
    trimmed=OUT/(item['id']+'_trim.wav');sf.write(trimmed,audio,sr,subtype='PCM_16')
    speed=max(1.,len(audio)/sr/slot) if slot else 1.
    if speed>1.15:raise ValueError('Additional voice requires excessive speedup')
    target=OUT/(item['id']+'_mix.wav')
    run(['-i',trimmed,'-af',f'highpass=f=75,lowpass=f=10500,atempo={speed},pan=stereo|c0=c0|c1=c0,loudnorm=I=-18:TP=-2:LRA=7,aresample={SR}','-ar',SR,'-ac',2,'-c:a','pcm_s16le',target],item['id']+'_audio')
    result,_=sf.read(target,dtype='float32',always_2d=True)
    return result,speed

intro_voice,_=prepare_voice(intro)
intro_frames=math.ceil((len(intro_voice)/SR+.9)*FPS)
cap=cv2.VideoCapture(str(STABLE));live_frames=int(cap.get(cv2.CAP_PROP_FRAME_COUNT));cap.release()
cg_frames=121*FPS;outro_frames=TOTAL_FRAMES-intro_frames-cg_frames-live_frames
if not 60<=outro_frames<=240:raise ValueError(f'Unexpected end-card length {outro_frames}')
intro_duration=intro_frames/FPS;live_start=(intro_frames+cg_frames)/FPS;live_duration=live_frames/FPS;outro_start=live_start+live_duration
plan={'duration':180.,'fps':FPS,'frames':TOTAL_FRAMES,'intro_duration':intro_duration,'intro_frames':intro_frames,
      'cg_source_start':8.,'cg_source_end':129.,'cg_duration':121.,'live_start':live_start,'live_duration':live_duration,
      'live_frames':live_frames,'outro_start':outro_start,'outro_duration':outro_frames/FPS,'outro_frames':outro_frames,
      'stabilized_source':STABLE.name,'introduction_display':intro['display']}
(OUT/'presentation_plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(plan,ensure_ascii=False),flush=True)

def ass_time(value):
    cs=round(value*100);h,cs=divmod(cs,360000);m,cs=divmod(cs,6000);s,c=divmod(cs,100)
    return f'{h}:{m:02d}:{s:02d}.{c:02d}'

def dialogue(start,end,text,style='Caption',tags=''):
    return f'Dialogue: 0,{ass_time(start)},{ass_time(end)},{style},,0,0,0,,{tags}{text}'

def write_ass(path,lines):
    header='''[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0
ScaledBorderAndShadow: yes
[V4+ Styles]
Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding
Style: Caption,Malgun Gothic,40,&H00FFFFFF,&H00FFFFFF,&H00101B23,&H990C141B,0,0,0,0,100,100,0,0,1,2,1,8,100,100,100,1
Style: IntroCaption,Malgun Gothic,40,&H00FFFFFF,&H00FFFFFF,&H00101B23,&H990C141B,0,0,0,0,100,100,0,0,1,1,0,2,80,80,40,1
Style: Card,Malgun Gothic,44,&H00EDF2F5,&H00FFFFFF,&H00101B23,&H990C141B,0,0,0,0,100,100,0,0,1,1,0,7,0,0,0,1
Style: Label,Malgun Gothic,27,&H008DCEEF,&H00FFFFFF,&H00201912,&H990C141B,0,0,0,0,100,100,0,0,1,2,0,7,70,70,40,1
Style: Credit,Malgun Gothic,21,&H00D9E4EA,&H00FFFFFF,&H00101B23,&H990C141B,0,0,0,0,100,100,0,0,1,1,0,7,0,0,0,1
[Events]
Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
'''
    path.write_text(header+'\n'.join(lines)+'\n',encoding='utf-8-sig')

def sentences(text,start,end,style='Caption'):
    parts=re.split(r'(?<=[.!?])\s+',text.strip());weights=[max(1,len(re.sub(r'\s','',part))) for part in parts];total=sum(weights);cursor=start;result=[];cues=[]
    for i,(part,weight) in enumerate(zip(parts,weights)):
        finish=end if i==len(parts)-1 else cursor+(end-start)*weight/total
        result.append(dialogue(cursor,finish,part,style));cues.append({'start':cursor,'end':finish,'text':part});cursor=finish
    return result,cues

voice=np.zeros((180*SR,2),np.float32);cues=[]
intro_lead=.35;voice[round(intro_lead*SR):round(intro_lead*SR)+len(intro_voice)]+=intro_voice
intro_lines,intro_cues=sentences(intro['display'],intro_lead,intro_lead+len(intro_voice)/SR,'IntroCaption');cues+=intro_cues
name_tags=r'{\an7\pos(1230,300)\fs36\c&HCFDCE6&\fad(350,350)}'
intro_lines += [dialogue(0,intro_duration,'울산과학대학교','Card',name_tags),
                dialogue(0,intro_duration,'캡스톤디자인 NEXUS','Card',r'{\an7\pos(1230,370)\fs40\c&HE6D092&\fad(350,350)}'),
                dialogue(0,intro_duration,'팀장 김형우','Card',r'{\an7\pos(1230,455)\fs70\fad(350,350)}'),
                dialogue(0,intro_duration,r'기계공학의 원리로 살펴보는\NAI기반 자동화 분류장치','Card',r'{\an7\pos(1230,565)\fs30\c&HBDD0DB&\fad(350,350)}')]
write_ass(OUT/'intro.ass',intro_lines)
cg_voice,_=sf.read(FINAL/'ARD_narration_only.wav',dtype='float32',always_2d=True)
voice[intro_frames*1600:(intro_frames+cg_frames)*1600]+=cg_voice[8*SR:129*SR]
timing=json.loads((HERE/'narration/timing.json').read_text(encoding='utf-8'))
for item in timing['scenes']:
    if item['id'] in range(2,15):
        _,more=sentences(item['text'],intro_duration+item['narration_start']-8,intro_duration+item['narration_end']-8);cues+=more
live_lines=[]
for item in live_records:
    clip,speed=prepare_voice(item,item['end']-item['start']-.3)
    start=item['start'];offset=round((live_start+start)*SR);voice[offset:offset+len(clip)]+=clip
    lines,part_cues=sentences(item['text'],start,start+len(clip)/SR);live_lines+=lines
    cues += [{'start':live_start+c['start'],'end':live_start+c['end'],'text':c['text']} for c in part_cues]
    item.update(fitted_duration=len(clip)/SR,speed_factor=speed)
for start,end,label in [(0,12,'실물 구동 확인 · 버튼 입력'),(12,28,'실물 구동 확인 · 플레이트 자세 전환'),(28,live_duration,'실물 구동 확인 · 터치 조이스틱')]:
    live_lines.append(dialogue(start,end,label,'Label'))
credits=r'Music: “Cylinder Five” · Chris Zabriskie\NCylinders · CC BY 4.0 · edited & mixed\Nchriszabriskie.com\Ncreativecommons.org/licenses/by/4.0/\NSFX: Kenney Interface Sounds · CC0'
live_lines.append(dialogue(max(0,live_duration-7),live_duration,credits,'Credit',r'{\an7\pos(1320,235)\fs21}'))
write_ass(OUT/'live.ass',live_lines)
write_ass(OUT/'outro.ass',[
    dialogue(0,outro_frames/FPS,'감사합니다','Card',r'{\an5\pos(960,430)\fs82\fad(250,500)}'),
    dialogue(0,outro_frames/FPS,'울산과학대학교 캡스톤디자인 NEXUS','Card',r'{\an5\pos(960,535)\fs38\fad(250,500)}'),
    dialogue(0,outro_frames/FPS,credits,'Credit',r'{\an7\pos(1200,680)\fs22}')])

# A new continuous music bed avoids restarting the soundtrack at the live cut.
music=HERE/'source_audio/Cylinder_Five.ogg';bed_path=OUT/'presentation_music.wav'
music_filters='[0:a]atrim=0:173.533,asetpts=PTS-STARTPTS[a];[1:a]atrim=0:24,asetpts=PTS-STARTPTS[b];[a][b]acrossfade=d=5:c1=tri:c2=tri,atrim=duration=180,loudnorm=I=-24:TP=-3:LRA=11,afade=t=in:d=2,afade=t=out:st=176:d=4,aresample=48000[m]'
run(['-i',music,'-i',music,'-filter_complex',music_filters,'-map','[m]','-ar',SR,'-ac',2,'-c:a','pcm_s16le',bed_path],'music_bed')
bed,_=sf.read(bed_path,dtype='float32',always_2d=True);bed=np.pad(bed[:len(voice)],((0,max(0,len(voice)-len(bed))),(0,0)))
block=240;power=np.mean(voice.reshape(-1,block,2)**2,axis=(1,2));targets=np.where(power>.00015,.35,1.);gains=[];value=1.
for target in targets:
    tau=.035 if target<value else .5;value+=(1-np.exp(-block/SR/tau))*(target-value);gains.append(value)
envelope=np.interp(np.arange(len(voice)),np.arange(len(gains))*block,gains).astype(np.float32)
mix=voice+bed*envelope[:,None]
# Quiet editorial cues mark section changes; these do not imitate machine sounds.
effects=[(.18,'open_001',.045),(intro_duration,'switch_002',.055),
         (intro_duration+19,'open_001',.05),(intro_duration+49,'tick_001',.045),
         (live_start,'open_002',.05),(outro_start,'confirmation_001',.045)]
for second,name,volume in effects:
    effect_path=HERE/'narration'/('sfx_'+name+'.wav')
    effect,_=sf.read(effect_path,dtype='float32',always_2d=True)
    offset=round(second*SR);n=min(len(effect),len(mix)-offset)
    mix[offset:offset+n]+=effect[:n]*(volume/max(float(np.max(np.abs(effect))),.01))
plan['editorial_effects']=effects
run(['-i',STABLE,'-vn','-af','highpass=f=100,lowpass=f=4500','-ar',SR,'-ac',2,'-c:a','pcm_s16le',OUT/'location_audio.wav'],'location_audio')
location,_=sf.read(OUT/'location_audio.wav',dtype='float32',always_2d=True);n=min(len(location),live_frames*1600)
mix[round(live_start*SR):round(live_start*SR)+n]+=location[:n]*.16
sf.write(FINAL/'ARD_Presentation_NarrationOnly.wav',voice,SR,subtype='PCM_16')
sf.write(OUT/'presentation_premaster.wav',mix,SR,subtype='FLOAT')
sf.write(OUT/'live_premaster.wav',mix[round(live_start*SR):round(outro_start*SR)],SR,subtype='FLOAT')
write_srt=lambda items:'\n'.join(f"{i+1}\n{srt_time(c['start'])} --> {srt_time(c['end'])}\n{c['text']}\n" for i,c in enumerate(items))
(FINAL/'ARD_FullPresentation_Korean.srt').write_text(write_srt(cues),encoding='utf-8-sig')
live_cues=[{'start':c['start']-live_start,'end':c['end']-live_start,'text':c['text']} for c in cues if c['start']>=live_start]
(FINAL/'ARD_LiveDemo_Korean.srt').write_text(write_srt(live_cues),encoding='utf-8-sig')

codec=['-c:v','h264_nvenc','-preset','p6','-profile:v','high','-level:v','4.1','-rc','vbr','-cq','19','-b:v','0','-g','60','-bf','2','-pix_fmt','yuv420p','-r','30','-an','-movflags','+faststart']
print('ENCODING_PRESENTATION_SEGMENTS',flush=True)
intro_filter=f'setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration={max(0,intro_duration-8):.9f},fps=30,drawbox=x=1160:y=260:w=680:h=430:color=0x07141C@0.84:t=fill,drawbox=x=0:y=960:w=1920:h=120:color=0x111B23:t=fill,subtitles=intro.ass,setsar=1'
run(['-t','8','-i',CG,'-vf',intro_filter,'-frames:v',intro_frames,*codec,OUT/'intro_segment.mp4'],'intro_video')
run(['-ss','8','-t','121','-i',CG,'-vf','setpts=PTS-STARTPTS,fps=30,setsar=1','-frames:v',cg_frames,*codec,OUT/'cg_middle.mp4'],'cg_middle')
run(['-i',STABLE,'-vf','setpts=PTS-STARTPTS,fps=30,subtitles=live.ass,setsar=1','-frames:v',live_frames,*codec,OUT/'live_annotated_video.mp4'],'live_video')
cap=cv2.VideoCapture(str(STABLE));cap.set(cv2.CAP_PROP_POS_FRAMES,live_frames-1);ok,last=cap.read();cap.release();assert ok
cv2.imwrite(str(OUT/'outro_background.png'),last)
run(['-loop','1','-i',OUT/'outro_background.png','-vf','drawbox=x=0:y=0:w=iw:h=ih:color=0x07141C@0.68:t=fill,subtitles=outro.ass,fps=30,setsar=1','-frames:v',outro_frames,*codec,OUT/'outro_segment.mp4'],'outro_video')
(OUT/'presentation_concat.txt').write_text("file 'intro_segment.mp4'\nfile 'cg_middle.mp4'\nfile 'live_annotated_video.mp4'\nfile 'outro_segment.mp4'\n",encoding='utf-8')
run(['-f','concat','-safe','0','-i',OUT/'presentation_concat.txt','-c:v','copy','-an',OUT/'presentation_video.mp4'],'concatenate')

def finish(video,audio,srt,destination,title):
    analysis=subprocess.run([str(FFMPEG),'-hide_banner','-i',str(audio),'-af','loudnorm=I=-16:TP=-1.7:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
    level=json.loads(re.findall(r'\{[^{}]+\}',analysis.stderr)[-1])
    filt=f"loudnorm=I=-16:TP=-1.7:LRA=11:measured_I={level['input_i']}:measured_TP={level['input_tp']}:measured_LRA={level['input_lra']}:measured_thresh={level['input_thresh']}:offset={level['target_offset']}:linear=false"
    run(['-i',video,'-i',audio,'-i',srt,'-map','0:v:0','-map','1:a:0','-map','2:s:0','-c:v','copy','-af',filt,'-c:a','aac','-b:a','256k','-ar',SR,'-c:s','mov_text','-metadata:s:s:0','language=kor','-disposition:s:0','0','-metadata','title='+title,'-metadata','comment=Voice: local myTTS. Music: Cylinder Five by Chris Zabriskie, CC BY 4.0, edited/mixed. SFX: Kenney CC0. Stabilized real footage supplied by user.','-movflags','+faststart',destination],destination.stem)
    return level
plan['mastering']=finish(OUT/'presentation_video.mp4',OUT/'presentation_premaster.wav',FINAL/'ARD_FullPresentation_Korean.srt',FINAL/'ARD_FullPresentation_1080p.mp4','AI기반 자동화 분류장치 — NEXUS 김형우 발표')
finish(OUT/'live_annotated_video.mp4',OUT/'live_premaster.wav',FINAL/'ARD_LiveDemo_Korean.srt',FINAL/'ARD_LiveDemo_Stabilized_1080p.mp4','AI기반 자동화 분류장치 — 실물 구동 시연')
plan['additional_narration']=records
(OUT/'presentation_plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
(FINAL/'presentation_plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
for name in ['ARD_FullPresentation_Korean','ARD_LiveDemo_Korean']:
    text=(FINAL/(name+'.srt')).read_text(encoding='utf-8-sig');(FINAL/(name+'.vtt')).write_text('WEBVTT\n\n'+re.sub(r'(?<=\d),(?=\d{3})','.',text),encoding='utf-8')
print('FULL_PRESENTATION_READY',flush=True)
