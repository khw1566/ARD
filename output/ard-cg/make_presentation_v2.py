"""Assemble local GPU v2 renders, private live footage, and the existing mix."""
from pathlib import Path
import argparse,json,math,re,subprocess,hashlib
import numpy as np
import cv2
from PIL import Image,ImageDraw,ImageFont,ImageFilter
from mix_audio import srt_time
HERE=Path(__file__).resolve().parent;OUT=HERE/'live'/'v2';OUT.mkdir(exist_ok=True)
FINAL=HERE/'final';FF=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe');SR=48000;FPS=30;N=1268
STABLE=HERE/'live/live_stabilized_clean.mp4';FONTS=HERE/'fonts';PAPER='#efede7';INK='#242a2b';MUTED='#596364';ORANGE='#c47f2c'
plan=json.loads((HERE/'live/presentation_plan.json').read_text(encoding='utf-8'))
captions=json.loads((HERE/'narration/cg_captions.json').read_text(encoding='utf-8'))
parser=argparse.ArgumentParser();parser.add_argument('--prepare-only',action='store_true');parser.add_argument('--reuse-renders',action='store_true');args=parser.parse_args()
def font(size,bold=False):return ImageFont.truetype(str(FONTS/('IBMPlexSansKR-Medium.ttf' if bold else 'IBMPlexSansKR-Regular.ttf')),size)
def wrap(draw,text,width,size):
    lines=[''];face=font(size)
    for word in text.split():
        current=(lines[-1]+' '+word).strip()
        if lines[-1] and draw.textlength(current,font=face)>width:lines.append(word)
        else:lines[-1]=current
    return lines
def split(text,start,end):
    parts=re.split(r'(?<=[.!?])\s+',text.strip());weights=[len(re.sub(r'\s','',p)) for p in parts];total=sum(weights);cursor=start;out=[]
    for i,(part,w) in enumerate(zip(parts,weights)):
        finish=end if i==len(parts)-1 else cursor+(end-start)*w/total
        out.append({'start':cursor,'end':finish,'text':part});cursor=finish
    return out
live=[]
for record in plan['additional_narration']:
    if record['id'].startswith('live_'):live+=split(record['text'],record['start'],record['start']+record['fitted_duration'])
full=captions['intro']+[{'start':13+c['start']-8,'end':13+c['end']-8,'text':c['text']} for c in captions['cg'] if 8<=c['start']<129]+[{'start':134+c['start'],'end':134+c['end'],'text':c['text']} for c in live]
def write_srt(stem,cues):
    text='\n'.join(f"{i+1}\n{srt_time(c['start'])} --> {srt_time(c['end'])}\n{c['text']}\n" for i,c in enumerate(cues))
    (FINAL/(stem+'.srt')).write_text(text,encoding='utf-8-sig');(FINAL/(stem+'.vtt')).write_text('WEBVTT\n\n'+re.sub(r'(?<=\d),(?=\d{3})','.',text),encoding='utf-8')
write_srt('ARD_FullPresentation_1080p_v2_Korean',full);write_srt('ARD_LiveDemo_1080p_v2_Korean',live)
# Covers both screen faces through the complete stabilized camera movement.
polygon=np.array([(1070,0),(1919,0),(1919,662),(1530,532),(1090,432)],np.int32)
mask=np.zeros((1080,1920),np.uint8);cv2.fillPoly(mask,[polygon],255);mask=cv2.GaussianBlur(mask,(0,0),9);cv2.imwrite(str(OUT/'privacy_mask.png'),mask)
(OUT/'privacy_mask.json').write_text(json.dumps({'polygon':polygon.tolist(),'feather_sigma':9,'blur_sigma':48,'purpose':'Obscure both PC screens, including personal email and AI chat content.'},indent=2),encoding='utf-8')
def frame_mask(frame):
    # The white tray passes in front of the screens. Preserve that foreground
    # silhouette while hiding screen content behind it throughout the clip.
    small=cv2.resize(frame,(640,360));hsv=cv2.cvtColor(small,cv2.COLOR_BGR2HSV)
    white=((hsv[:,:,1]<88)&(hsv[:,:,2]>135)).astype(np.uint8)*255
    roi=np.zeros_like(white);roi[70:220,240:400]=white[70:220,240:400]
    roi=cv2.morphologyEx(roi,cv2.MORPH_CLOSE,np.ones((3,3),np.uint8))
    count,components,stats,centers=cv2.connectedComponentsWithStats(roi)
    candidates=[i for i in range(1,count) if stats[i,cv2.CC_STAT_AREA]>300 and 265<centers[i][0]<375 and 90<centers[i][1]<200]
    if not candidates:return mask
    index=min(candidates,key=lambda i:(centers[i][0]-310)**2+(centers[i][1]-140)**2)
    points=np.column_stack(np.nonzero(components==index)[::-1]).astype(np.int32)*3
    foreground=np.zeros_like(mask);cv2.fillConvexPoly(foreground,cv2.convexHull(points),255)
    foreground=cv2.dilate(foreground,np.ones((13,13),np.uint8));foreground=cv2.GaussianBlur(foreground,(0,0),3)
    return np.round(mask.astype(np.float32)*(1-foreground.astype(np.float32)/255)).astype(np.uint8)
def privacy(frame):
    blurred=cv2.GaussianBlur(frame,(0,0),48);a=frame_mask(frame)[:,:,None].astype(np.float32)/255
    return np.round(frame*(1-a)+blurred*a).astype(np.uint8)
labels=['실물 구동 · 버튼 입력','실물 구동 · 플레이트 자세 전환','실물 구동 · 터치 조이스틱']
states=[]
for i in range(N):
    t=i/30;cue=next((c['text'] for c in live if c['start']<=t<c['end']),'');key=(0 if t<12 else 1 if t<28 else 2,cue)
    if not states or states[-1]['key']!=key:states.append({'frame':i,'key':key})
concat=['ffconcat version 1.0']
for i,state in enumerate(states):
    index,text=state['key'];img=Image.new('RGBA',(1920,1080));d=ImageDraw.Draw(img)
    d.rectangle((0,0,1920,104),fill=PAPER)
    if text:
        lines=wrap(d,text,1740,32);top=(104-42*len(lines))//2
        for j,line in enumerate(lines):d.text((960,top+j*42),line,font=font(32),fill=INK,anchor='mt')
    d.rectangle((84,806,804,944),fill=PAPER);d.rectangle((84,806,88,944),fill=ORANGE)
    d.text((112,836),'실물 구동',font=font(43,True),fill=INK)
    d.text((112,898),labels[index].split(' · ')[1],font=font(23),fill=MUTED)
    name=f'overlay_{i:03d}.png';img.save(OUT/name)
    n=(states[i+1]['frame'] if i+1<len(states) else N)-state['frame']
    concat += [f"file '{name}'",'option framerate 30',f'duration {n/30:.12f}']
concat += [f"file 'overlay_{len(states)-1:03d}.png'",'option framerate 30']
(OUT/'live_overlays.ffconcat').write_text('\n'.join(concat)+'\n',encoding='utf-8')
credit='“Cylinder Five” · Chris Zabriskie\nCylinders · CC BY 4.0 · edited & mixed\nchriszabriskie.com/cylinders/\ncreativecommons.org/licenses/by/4.0/\n효과음: Kenney Interface Sounds · CC0'
outro=Image.new('RGBA',(1920,1080));d=ImageDraw.Draw(outro)
d.text((960,362),'감사합니다',font=font(80,True),fill=PAPER,anchor='mt')
d.text((960,478),'울산과학대학교 캡스톤디자인 NEXUS',font=font(34),fill=PAPER,anchor='mt')
d.text((960,535),'팀장 김형우',font=font(28),fill=PAPER,anchor='mt')
for i,line in enumerate(credit.splitlines()):d.text((110,778+i*39),line,font=font(23),fill=PAPER)
outro.save(OUT/'outro_overlay.png')
cap=cv2.VideoCapture(str(STABLE));times=[0,3,8,12,16,20,24,28,32,36,40,42]
sheet=Image.new('RGB',(1920,384*len(times)),'#242a2b');draw=ImageDraw.Draw(sheet)
for i,t in enumerate(times):
    cap.set(cv2.CAP_PROP_POS_MSEC,t*1000);ok,frame=cap.read();assert ok,t
    original=frame.copy();cv2.polylines(original,[polygon],True,(0,160,255),4);p=privacy(frame)
    for column,array in enumerate([original,p]):
        image=Image.fromarray(cv2.cvtColor(array,cv2.COLOR_BGR2RGB));image.thumbnail((960,540));image=image.resize((640,360));sheet.paste(image,(column*960,i*384+24))
    draw.text((10,i*384+4),f'{t}s / privacy boundary left, filtered right',fill='white')
cap.set(cv2.CAP_PROP_POS_FRAMES,N-1);ok,last=cap.read();cap.release();assert ok
last=cv2.GaussianBlur(privacy(last),(0,0),24);last=(last*.34).astype(np.uint8);cv2.imwrite(str(OUT/'outro_background.png'),last)
sheet.save(OUT/'privacy_contact.jpg',quality=92)
if args.prepare_only:print('V2_ASSETS_PREPARED',len(states),flush=True);raise SystemExit
def run(params,name):
    with (OUT/(name+'.log')).open('w',encoding='utf-8') as log:subprocess.run([str(FF),'-hide_banner','-y',*map(str,params)],cwd=OUT,stdout=log,stderr=subprocess.STDOUT,check=True)
codec=['-c:v','h264_nvenc','-preset','p6','-profile:v','high','-level:v','4.1','-rc','vbr','-cq','18','-b:v','0','-g','60','-bf','2','-pix_fmt','yuv420p','-r','30','-an','-movflags','+faststart']
job=Path((HERE/'render_jobs/latest.txt').read_text(encoding='utf-8').strip());state=json.loads((job/'status.json').read_text(encoding='utf-8'))
assert state['status']=='complete' and state['result']['frames']==4020,state
print('ENCODING_V2',job,flush=True)
if not args.reuse_renders:run(['-fflags','+genpts','-r','30','-i',job/'video.h264','-vf','fps=30,setsar=1','-frames:v',4020,*codec,OUT/'cg_intro_v2.mp4'],'cg_intro')
mask_path=OUT/'privacy_mask.mkv'
if args.reuse_renders:assert mask_path.is_file() and (OUT/'cg_intro_v2.mp4').is_file()
if not args.reuse_renders:
 with (OUT/'mask_video.log').open('w',encoding='utf-8') as log:
    proc=subprocess.Popen([str(FF),'-hide_banner','-y','-f','rawvideo','-pix_fmt','gray','-s','1920x1080','-r','30','-i','pipe:0','-an','-c:v','ffv1','-level','3','-threads','2',str(mask_path)],stdin=subprocess.PIPE,stdout=log,stderr=log)
    cap=cv2.VideoCapture(str(STABLE));count=0
    while True:
        ok,frame=cap.read()
        if not ok:break
        proc.stdin.write(frame_mask(frame).tobytes());count+=1
    cap.release();proc.stdin.close();assert proc.wait()==0 and count==N
print('PRIVACY_MASK_READY',N,flush=True)
filters='[0:v]fps=30,settb=1/30,setpts=N,split[main][b];[b]gblur=sigma=48:steps=3,format=rgba[blur];[1:v]fps=30,settb=1/30,setpts=N,format=gray[m];[blur][m]alphamerge[covered];[main][covered]overlay=shortest=1:format=auto[private];[2:v]fps=30,settb=1/30,setpts=N[o];[private][o]overlay=eof_action=repeat:format=auto,setsar=1[v]'
run(['-i',STABLE,'-i',mask_path,'-f','concat','-safe','0','-i',OUT/'live_overlays.ffconcat','-filter_complex',filters,'-map','[v]','-frames:v',N,*codec,OUT/'live_v2.mp4'],'live_private')
run(['-loop','1','-i',OUT/'outro_background.png','-loop','1','-i',OUT/'outro_overlay.png','-filter_complex','[1:v]format=rgba,fade=t=in:st=0:d=0.35:alpha=1[o];[0:v][o]overlay,setsar=1[v]','-map','[v]','-frames:v',112,*codec,OUT/'outro_v2.mp4'],'outro')
(OUT/'concat.txt').write_text("file 'cg_intro_v2.mp4'\nfile 'live_v2.mp4'\nfile 'outro_v2.mp4'\n",encoding='utf-8')
run(['-f','concat','-safe','0','-i',OUT/'concat.txt','-c:v','copy','-an',OUT/'picture_v2.mp4'],'concat')
def finish(video,audio,dest):
    analysis=subprocess.run([str(FF),'-hide_banner','-i',str(audio),'-af','loudnorm=I=-16:TP=-1.7:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
    level=json.loads(re.findall(r'\{[^{}]+\}',analysis.stderr)[-1]);filt=f"loudnorm=I=-16:TP=-1.7:LRA=11:measured_I={level['input_i']}:measured_TP={level['input_tp']}:measured_LRA={level['input_lra']}:measured_thresh={level['input_thresh']}:offset={level['target_offset']}:linear=false"
    run(['-i',video,'-i',audio,'-map','0:v:0','-map','1:a:0','-map_metadata','-1','-map_chapters','-1','-c:v','copy','-af',filt,'-c:a','aac','-b:a','256k','-ar',SR,'-sn','-metadata','title=AI기반 자동화 분류장치 · NEXUS 김형우 발표 v2','-metadata','comment=Music: Cylinder Five by Chris Zabriskie, CC BY 4.0, edited/mixed. SFX: Kenney CC0. Private PC screens obscured. Korean captions are burned in only.','-movflags','+faststart',dest],dest.stem)
finish(OUT/'picture_v2.mp4',HERE/'live/presentation_premaster.wav',FINAL/'ARD_FullPresentation_1080p_v2.mp4')
finish(OUT/'live_v2.mp4',HERE/'live/live_premaster.wav',FINAL/'ARD_LiveDemo_Stabilized_1080p_v2.mp4')
plan.update(revision='v2',caption_track_embedded=False,captions_external=True,privacy_mask=str((OUT/'privacy_mask.json').relative_to(HERE)),render_job=str(job),rendered_cg_intro_frames=4020)
(FINAL/'presentation_v2_plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
print('PRESENTATION_V2_READY',flush=True)
