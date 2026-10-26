"""Full media checks for v2; one baked caption layer and no subtitle streams."""
from pathlib import Path
import subprocess,json,re,hashlib,cv2
from PIL import Image,ImageDraw
HERE=Path(__file__).resolve().parent;FINAL=HERE/'final';QA=HERE/'live/v2/qa';QA.mkdir(exist_ok=True)
FF=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe');reports=[]
for stem,frames,duration,times in [
 ('ARD_FullPresentation_1080p_v2',5400,180.,[4,10,17,26,36,43,50,59,70,80,91,103,112,120,124,127,129,132,135,149,165,175,178]),
 ('ARD_LiveDemo_Stabilized_1080p_v2',1268,1268/30,[0,3,8,12,16,20,24,28,32,36,40,42])]:
 video=FINAL/(stem+'.mp4')
 probe=json.loads(subprocess.run([str(FF.with_name('ffprobe.exe')),'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(video)],capture_output=True,text=True,encoding='utf-8',check=True).stdout)
 v=next(s for s in probe['streams'] if s['codec_type']=='video');a=next(s for s in probe['streams'] if s['codec_type']=='audio')
 assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_read_frames']))==(1920,1080,'30/1',frames)
 assert a['sample_rate']=='48000' and a['channels']==2
 assert not any(s['codec_type']=='subtitle' for s in probe['streams'])
 assert not any('location' in key.lower() for item in [probe['format'],*probe['streams']] for key in item.get('tags',{}))
 assert abs(float(probe['format']['duration'])-duration)<.1
 result=subprocess.run([str(FF),'-hide_banner','-i',str(video),'-vf','blackdetect=d=0.5:pix_th=0.015:pic_th=0.99','-af','loudnorm=I=-16:TP=-1.7:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
 (QA/(stem+'_decode.log')).write_text(result.stderr,encoding='utf-8')
 black=re.findall(r'black_start:[^\r\n]+',result.stderr);assert not black,black
 level=json.loads(re.findall(r'\{[^{}]+\}',result.stderr)[-1]);assert -17<float(level['input_i'])<-15 and float(level['input_tp'])<-.5
 sheet=Image.new('RGB',(1920,384*((len(times)+2)//3)),'#242a2b')
 draw=ImageDraw.Draw(sheet);cap=cv2.VideoCapture(str(video))
 for i,t in enumerate(times):
  cap.set(cv2.CAP_PROP_POS_MSEC,t*1000);ok,frame=cap.read();assert ok,t
  img=Image.fromarray(cv2.cvtColor(frame,cv2.COLOR_BGR2RGB));img.save(QA/f'{stem}_{t:03d}.jpg',quality=94)
  if stem=='ARD_FullPresentation_1080p_v2' and t==4:img.save(FINAL/'presentation_v2_poster.jpg',quality=94)
  if stem.startswith('ARD_LiveDemo') and t==8:img.save(FINAL/'live_v2_poster.jpg',quality=94)
  img.thumbnail((640,360));x=i%3*640;y=i//3*384;sheet.paste(img,(x,y+24));draw.text((x+10,y+5),f'{t:03d} sec',fill='white')
 cap.release();sheet.save(FINAL/(stem+'_contact.jpg'),quality=94)
 report={'file':video.name,'bytes':video.stat().st_size,'sha256':hashlib.sha256(video.read_bytes()).hexdigest(),'duration':float(probe['format']['duration']),
         'decoded_frames':int(v['nb_read_frames']),'size':[v['width'],v['height']],'fps':v['r_frame_rate'],'audio_sample_rate':a['sample_rate'],
         'lufs':float(level['input_i']),'true_peak_dbtp':float(level['input_tp']),'subtitle_streams':0,'black_segments':black,'full_decode_passed':True,'sample_times':times}
 reports.append(report);print(json.dumps(report,ensure_ascii=False),flush=True)
for stem,limit in [('ARD_FullPresentation_1080p_v2_Korean',180),('ARD_LiveDemo_1080p_v2_Korean',42.3)]:
 data=(FINAL/(stem+'.srt')).read_text(encoding='utf-8-sig');prev=0
 for stamp in re.findall(r'(\d+):(\d+):(\d+),(\d+) --> (\d+):(\d+):(\d+),(\d+)',data):
  n=list(map(int,stamp));start=n[0]*3600+n[1]*60+n[2]+n[3]/1000;end=n[4]*3600+n[5]*60+n[6]+n[7]/1000
  assert start>=prev-.001 and start<end<=limit;prev=end
source=HERE.parents[1]/'영상제작용 CAD파일/20261026_195051.mp4';source_hash=hashlib.sha256(source.read_bytes()).hexdigest();assert source_hash=='b880d787654870746a72fbce5154761309add23927a181bdccf6cae49238fec9'
old=FINAL/'ARD_FullPresentation_1080p.mp4';old_hash=hashlib.sha256(old.read_bytes()).hexdigest();assert old_hash=='e38085c48341399b1d69ba2b0e70b985c3ab2830438870329fb7cdadd7f029ea'
(FINAL/'presentation_v2_quality_report.json').write_text(json.dumps({'outputs':reports,'source_sha256':source_hash,'v1_sha256':old_hash,'originals_unchanged':True,'external_srt_timestamps_passed':True,'privacy_review':'12 sampled times, foreground-aware screen mask; not an exhaustive manual frame-by-frame inspection.','credits_occurrences':'outro only; none in live overlays','private_voice_uploaded':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('V2_VALIDATION_PASSED',flush=True)
