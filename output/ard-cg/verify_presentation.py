"""Validate delivered presentation media and keep representative frame evidence."""
from pathlib import Path
import hashlib,json,re,subprocess
import cv2
from PIL import Image,ImageDraw

HERE=Path(__file__).resolve().parent
FINAL=HERE/'final';QA=HERE/'live'/'qa';QA.mkdir(exist_ok=True)
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
plan=json.loads((FINAL/'presentation_plan.json').read_text(encoding='utf-8'))
reports=[]
for name,frames,seconds,times in [
    ('ARD_FullPresentation_1080p',5400,180.,[4,10,17,35,49,59,73,92,112,128,136,149,165,174,178]),
    ('ARD_LiveDemo_Stabilized_1080p',plan['live_frames'],plan['live_duration'],[2,8,16,24,31,38,41])]:
    video=FINAL/(name+'.mp4')
    probe=json.loads(subprocess.run([str(FFMPEG.with_name('ffprobe.exe')),'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(video)],capture_output=True,text=True,encoding='utf-8',check=True).stdout)
    v=next(s for s in probe['streams'] if s['codec_type']=='video');a=next(s for s in probe['streams'] if s['codec_type']=='audio')
    assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_read_frames']))==(1920,1080,'30/1',frames),(name,v)
    assert a['channels']==2 and a['sample_rate']=='48000'
    assert abs(float(probe['format']['duration'])-seconds)<.1
    assert any(s['codec_type']=='subtitle' and s.get('tags',{}).get('language')=='kor' for s in probe['streams'])
    result=subprocess.run([str(FFMPEG),'-hide_banner','-i',str(video),'-vf','blackdetect=d=0.5:pix_th=0.015:pic_th=0.99','-af','loudnorm=I=-16:TP=-1.7:LRA=11:print_format=json','-f','null','NUL'],capture_output=True,text=True,encoding='utf-8',check=True)
    (QA/(name+'_decode.log')).write_text(result.stderr,encoding='utf-8')
    black=re.findall(r'black_start:[^\r\n]+',result.stderr);assert not black,black
    level=json.loads(re.findall(r'\{[^{}]+\}',result.stderr)[-1])
    assert -17<float(level['input_i'])<-15,level
    assert float(level['input_tp'])<-.5,level
    cols=3;sheet=Image.new('RGB',(1920,384*((len(times)+cols-1)//cols)),'#111b23');draw=ImageDraw.Draw(sheet)
    cap=cv2.VideoCapture(str(video))
    for i,second in enumerate(times):
        cap.set(cv2.CAP_PROP_POS_MSEC,second*1000);ok,frame=cap.read();assert ok,(name,second)
        image=Image.fromarray(cv2.cvtColor(frame,cv2.COLOR_BGR2RGB));image.save(QA/f'{name}_{second:03d}.jpg',quality=93)
        if name=='ARD_FullPresentation_1080p' and second==4:image.save(FINAL/'presentation_poster.jpg',quality=94)
        if name.startswith('ARD_LiveDemo') and second==8:image.save(FINAL/'live_poster.jpg',quality=94)
        image.thumbnail((640,360));x=i%cols*640;y=i//cols*384;sheet.paste(image,(x,y+24));draw.text((x+10,y+5),f'{second:03d} sec',fill='white')
    cap.release();sheet.save(FINAL/(name+'_contact.jpg'),quality=93)
    report={'file':video.name,'bytes':video.stat().st_size,'sha256':hashlib.sha256(video.read_bytes()).hexdigest(),
            'duration_seconds':float(probe['format']['duration']),'width':v['width'],'height':v['height'],'fps':v['r_frame_rate'],
            'decoded_frames':int(v['nb_read_frames']),'video_codec':v['codec_name'],'audio_codec':a['codec_name'],
            'channels':a['channels'],'sample_rate':a['sample_rate'],'integrated_lufs':float(level['input_i']),
            'true_peak_dbtp':float(level['input_tp']),'detected_black_segments':black,'full_decode_passed':True,'sample_times':times}
    reports.append(report);print(json.dumps(report,ensure_ascii=False),flush=True)
source=HERE.parents[1]/'영상제작용 CAD파일'/'20261026_195051.mp4'
source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
assert source_hash=='b880d787654870746a72fbce5154761309add23927a181bdccf6cae49238fec9'
for stem,duration in [('ARD_FullPresentation_Korean',180),('ARD_LiveDemo_Korean',plan['live_duration'])]:
    data=(FINAL/(stem+'.srt')).read_text(encoding='utf-8-sig')
    stamps=re.findall(r'(\d+):(\d+):(\d+),(\d+) --> (\d+):(\d+):(\d+),(\d+)',data)
    end_previous=0.
    for stamp in stamps:
        n=list(map(int,stamp));start=n[0]*3600+n[1]*60+n[2]+n[3]/1000;end=n[4]*3600+n[5]*60+n[6]+n[7]/1000
        assert start>=end_previous-.001 and start<end<=duration+.001,(stem,start,end,end_previous)
        end_previous=end
result={'outputs':reports,'source_file':source.name,'source_sha256':source_hash,'source_unchanged':True,
        'subtitle_timestamps_passed':True,'private_voice_uploaded':False,
        'stabilization':json.loads((HERE/'live'/'selected_stabilized.json').read_text(encoding='utf-8')),
        'scope':'Presentation edit of supplied manual-operation footage, not proof of automatic sorting or encoder feedback.'}
(FINAL/'presentation_quality_report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('PRESENTATION_VALIDATION_PASSED',flush=True)
