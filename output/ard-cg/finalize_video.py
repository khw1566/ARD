"""Encode the completed deterministic H.264 frames and mix into a review MP4."""
from pathlib import Path
import argparse
import json
import re
import subprocess

HERE=Path(__file__).resolve().parent
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
FFPROBE=FFMPEG.with_name('ffprobe.exe')
job=Path((HERE/'render_jobs'/'latest.txt').read_text(encoding='utf-8').strip())
state=json.loads((job/'status.json').read_text(encoding='utf-8'))
if state['status']!='complete' or state['result']['encodedFrames']!=4050:
    raise SystemExit('Incomplete frame export')
final=HERE/'final'
video=final/'ARD_CG_MyVoice_1080p.mp4'
parser=argparse.ArgumentParser();parser.add_argument('--audio-only',action='store_true');options=parser.parse_args()
analysis=subprocess.run([str(FFMPEG),'-hide_banner','-i',str(final/'ARD_final_mix.wav'),'-af','loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
levels=json.loads(re.findall(r'\{[^{}]+\}',analysis.stderr)[-1])
filters=f"loudnorm=I=-16:TP=-1.5:LRA=11:measured_I={levels['input_i']}:measured_TP={levels['input_tp']}:measured_LRA={levels['input_lra']}:measured_thresh={levels['input_thresh']}:offset={levels['target_offset']}:linear=false"
input_video=['-i',str(video)] if options.audio_only else ['-fflags','+genpts','-r','30','-i',str(job/'video.h264')]
video_codec=['-c:v','copy'] if options.audio_only else ['-c:v','h264_nvenc','-preset','p6','-tune','hq','-rc','vbr','-cq','19','-b:v','0','-pix_fmt','yuv420p']
destination=video.with_name(video.stem+'.update.mp4') if options.audio_only else video
args=[str(FFMPEG),'-hide_banner','-y',*input_video,
      '-i',str(final/'ARD_final_mix.wav'),'-i',str(final/'ARD_Korean_transcript.srt'),
      '-map','0:v:0','-map','1:a:0','-map','2:s:0',*video_codec,'-c:a','aac','-b:a','256k','-ar','48000',
      '-af',filters,'-c:s','mov_text','-metadata:s:s:0','language=kor','-disposition:s:0','0',
      '-metadata','title=AI 기반 자동화 분류장치 — 기구학 CG',
      '-metadata','comment=Narration: local myTTS user voice. Music: Cylinder Five by Chris Zabriskie, CC BY 4.0, trimmed/mixed. SFX: Kenney Interface Sounds, CC0. See CREDITS.txt.',
      '-t','135','-movflags','+faststart',str(destination)]
print('FINAL_ENCODING',flush=True)
subprocess.run(args,check=True,stdout=subprocess.DEVNULL,stderr=(job/'final_encode.log').open('w',encoding='utf-8'))
if options.audio_only:destination.replace(video)
(final/'mastering_levels.json').write_text(json.dumps(levels,indent=2),encoding='utf-8')
probe=subprocess.run([str(FFPROBE),'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(video)],check=True,capture_output=True,text=True,encoding='utf-8')
data=json.loads(probe.stdout)
(final/'video_probe.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
for stream in data['streams']:
    if stream['codec_type']=='video':
        assert int(stream['nb_read_frames'])==4050
        assert (stream['width'],stream['height'])==(1920,1080)
        assert stream['r_frame_rate']=='30/1'
    elif stream['codec_type']=='audio':
        assert stream['sample_rate']=='48000' and stream['channels']==2
assert abs(float(data['format']['duration'])-135)<.15
print(json.dumps({'video':str(video),'bytes':video.stat().st_size,'duration':data['format']['duration'],'frames':4050,'resolution':'1920x1080','fps':30},ensure_ascii=False),flush=True)
