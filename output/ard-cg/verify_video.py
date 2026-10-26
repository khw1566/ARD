"""Decode the finished movie, check loudness/frames, and inspect sampled cuts."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import cv2
from PIL import Image,ImageDraw

HERE=Path(__file__).resolve().parent
final=HERE/'final';video=final/'ARD_CG_MyVoice_1080p.mp4'
job=Path((HERE/'render_jobs'/'latest.txt').read_text(encoding='utf-8').strip())
ffmpeg=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
probe=json.loads(subprocess.run([str(ffmpeg.with_name('ffprobe.exe')),'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(video)],capture_output=True,text=True,encoding='utf-8',check=True).stdout)
v=next(stream for stream in probe['streams'] if stream['codec_type']=='video');a=next(stream for stream in probe['streams'] if stream['codec_type']=='audio')
assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_read_frames']))==(1920,1080,'30/1',4050)
assert a['channels']==2 and a['sample_rate']=='48000'
assert abs(float(probe['format']['duration'])-135)<.1
decoded=subprocess.run([str(ffmpeg),'-hide_banner','-i',str(video),'-vf','blackdetect=d=0.5:pix_th=0.015:pic_th=0.99','-af','loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
(job/'decode_loudness_check.log').write_text(decoded.stderr,encoding='utf-8')
black=re.findall(r'black_start:[^\r\n]+',decoded.stderr)
assert not black,black
levels=json.loads(re.findall(r'\{[^{}]+\}',decoded.stderr)[-1])
assert -17<float(levels['input_i'])<-15
assert float(levels['input_tp'])<-0.5
times=[4,12,22,30,38,46,54,60,68,77,87,97,109,121,132]
sheet=Image.new('RGB',(1920,5*384),'#111b23');draw=ImageDraw.Draw(sheet)
cap=cv2.VideoCapture(str(video));samples=[]
for i,second in enumerate(times):
    cap.set(cv2.CAP_PROP_POS_MSEC,second*1000);ok,frame=cap.read();assert ok,second
    image=Image.fromarray(cv2.cvtColor(frame,cv2.COLOR_BGR2RGB));image.save(job/'qa'/f'final_{second:03d}.jpg',quality=90)
    if second==38:image.save(final/'poster.jpg',quality=94)
    image.thumbnail((640,360));x=i%3*640;y=i//3*384
    sheet.paste(image,(x,y+24));draw.text((x+10,y+5),f'Cut {i+1:02d} / {second:03d} sec',fill='white');samples.append(second)
cap.release();sheet.save(final/'contact_sheet.jpg',quality=93)
srt=(final/'ARD_Korean_transcript.srt').read_text(encoding='utf-8-sig')
(final/'ARD_Korean_transcript.vtt').write_text('WEBVTT\n\n'+re.sub(r'(?<=\d),(?=\d{3})','.',srt),encoding='utf-8')
report={'file':video.name,'sha256':hashlib.sha256(video.read_bytes()).hexdigest(),'bytes':video.stat().st_size,
        'duration_seconds':float(probe['format']['duration']),'video_codec':v['codec_name'],'audio_codec':a['codec_name'],
        'width':v['width'],'height':v['height'],'fps':v['r_frame_rate'],'decoded_frames':int(v['nb_read_frames']),
        'sample_rate':a['sample_rate'],'channels':a['channels'],'integrated_lufs':float(levels['input_i']),
        'true_peak_dbtp':float(levels['input_tp']),'loudness_range_lu':float(levels['input_lra']),
        'detected_black_segments':black,'representative_scene_times':samples,'full_decode_passed':True,
        'personal_voice_external_upload':False,'physical_device_verification':False}
(final/'quality_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False),flush=True)
