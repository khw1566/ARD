"""Two-pass stabilization of supplied live footage; source stays unchanged."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SOURCE=ROOT/'영상제작용 CAD파일'/'20261026_195051.mp4'
OUT=HERE/'live';OUT.mkdir(exist_ok=True)
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
environment=os.environ.copy();environment['OMP_NUM_THREADS']='6'
before=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
probe=json.loads(subprocess.run([str(FFMPEG.with_name('ffprobe.exe')),'-v','error','-show_entries','format=duration,size:stream=index,codec_type,codec_name,width,height,avg_frame_rate,sample_rate,channels','-of','json',str(SOURCE)],check=True,capture_output=True,text=True).stdout)
(OUT/'source_probe.json').write_text(json.dumps(probe,ensure_ascii=False,indent=2),encoding='utf-8')
motion=OUT/'camera_motion.trf'
if not motion.exists():
    print('STABILIZATION_ANALYSIS',flush=True)
    args=[str(FFMPEG),'-hide_banner','-y','-threads','6','-i',str(SOURCE),'-vf','fps=30,vidstabdetect=shakiness=6:accuracy=15:stepsize=6:mincontrast=0.1:result=camera_motion.trf:fileformat=ascii','-an','-f','null','NUL']
    with (OUT/'detect.log').open('w',encoding='utf-8') as log:
        subprocess.run(args,check=True,cwd=OUT,env=environment,stdout=log,stderr=subprocess.STDOUT)
print('STABILIZATION_TRANSFORM',flush=True)
filters='fps=30,vidstabtransform=input=camera_motion.trf:smoothing=35:optalgo=gauss:optzoom=1:zoom=1:interpol=bicubic:crop=black'
args=[str(FFMPEG),'-hide_banner','-y','-threads','6','-i',str(SOURCE),'-vf',filters,'-c:v','h264_nvenc','-preset','p6','-rc','vbr','-cq','19','-b:v','0','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart',str(OUT/'live_stabilized_clean.mp4')]
with (OUT/'transform.log').open('w',encoding='utf-8') as log:
    subprocess.run(args,check=True,cwd=OUT,env=environment,stdout=log,stderr=subprocess.STDOUT)
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==before
report={'source':str(SOURCE.relative_to(ROOT)),'source_sha256':before,'source_unchanged':True,
        'output':'live_stabilized_clean.mp4','filter':filters,'analysis_shakiness':6,'analysis_accuracy':15,
        'warning':'Two-pass global camera stabilization; moving plate and hands are not tracked as the camera target.'}
(OUT/'stabilization.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('STABILIZATION_READY',flush=True)
