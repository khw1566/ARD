"""Fit local XTTS clips to the CG cuts and mix licensed background audio."""
from pathlib import Path
import hashlib
import json
import subprocess
import numpy as np
import soundfile as sf

HERE=Path(__file__).resolve().parent
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
SR=48000
DURATION=135

def run(args):
    subprocess.run([str(FFMPEG),'-hide_banner','-loglevel','error','-y',*map(str,args)],check=True)

def trim_speech(data,sr):
    mono=data.mean(1) if data.ndim>1 else data
    block=max(1,sr//100)
    energy=np.sqrt(np.mean(np.pad(mono,(0,(-len(mono))%block)).reshape(-1,block)**2,axis=1))
    active=np.flatnonzero(energy>max(.0025,float(energy.max())*.012))
    if not len(active):raise ValueError('Silent narration')
    start=max(0,int(active[0]*block-.08*sr));end=min(len(mono),int((active[-1]+1)*block+.14*sr))
    return mono[start:end]

def srt_time(seconds):
    value=round(seconds*1000);hours,value=divmod(value,3600000);minutes,value=divmod(value,60000);secs,ms=divmod(value,1000)
    return f'{hours:02d}:{minutes:02d}:{secs:02d},{ms:03d}'

def main():
    narration=HERE/'narration';final=HERE/'final';final.mkdir(exist_ok=True)
    manifest=json.loads((narration/'manifest.json').read_text(encoding='utf-8'))
    voice=np.zeros((DURATION*SR,2),dtype=np.float32);timings=[];subtitles=[]
    for item in manifest['scenes']:
        wav,sr=sf.read(narration/item['file'],dtype='float32');wav=trim_speech(wav,sr)
        trimmed=narration/f"C{item['id']:02d}_trim.wav";sf.write(trimmed,wav,sr,subtype='PCM_16')
        available=item['end']-item['start']-.46;rate=max(1.,len(wav)/sr/available)
        if rate>1.18:raise ValueError(f"C{item['id']} needs excessive speedup {rate}")
        fitted=narration/f"C{item['id']:02d}_mix.wav"
        filters=f'highpass=f=75,lowpass=f=10500,atempo={rate:.8f},pan=stereo|c0=c0|c1=c0,loudnorm=I=-18:TP=-2:LRA=7,aresample={SR}'
        run(['-i',trimmed,'-af',filters,'-ar',SR,'-ac',2,'-c:a','pcm_s16le',fitted])
        clip,_=sf.read(fitted,dtype='float32',always_2d=True)
        start=item['start']+.23;offset=round(start*SR)
        voice[offset:offset+len(clip)]+=clip
        record={'id':item['id'],'title':item['title'],'start':item['start'],'end':item['end'],
                'logical_start':item['start'],'logical_end':item['end'],'narration_start':start,
                'narration_end':start+len(clip)/SR,'speed_factor':rate,'text':item['text']}
        timings.append(record)
        subtitles.append(f"{item['id']}\n{srt_time(start)} --> {srt_time(record['narration_end'])}\n{item['text']}\n")
        print(json.dumps({'cut':item['id'],'speech_seconds':round(len(clip)/SR,3),'speed':round(rate,3)},ensure_ascii=False),flush=True)
    sf.write(final/'ARD_narration_only.wav',voice,SR,subtype='PCM_16')
    music=HERE/'source_audio'/'Cylinder_Five.ogg';music_wav=narration/'music_bed.wav'
    run(['-i',music,'-t',DURATION,'-af',f'loudnorm=I=-24:TP=-3:LRA=11,afade=t=in:d=2,afade=t=out:st=131:d=4,aresample={SR}','-ar',SR,'-ac',2,'-c:a','pcm_s16le',music_wav])
    bed,_=sf.read(music_wav,dtype='float32',always_2d=True);bed=bed[:len(voice)]
    if len(bed)<len(voice):bed=np.pad(bed,((0,len(voice)-len(bed)),(0,0)))
    # Speech-aware music ducking: short attack, gentle recovery between cuts.
    block=240;power=np.mean(voice.reshape(-1,block,2)**2,axis=(1,2));targets=np.where(power>.00015,.35,1.)
    gains=np.empty(len(targets),dtype=np.float32);value=1.
    for i,target in enumerate(targets):
        tau=.035 if target<value else .5;alpha=1-np.exp(-block/SR/tau);value+=alpha*(target-value);gains[i]=value
    envelope=np.interp(np.arange(len(voice)),np.arange(len(gains))*block,gains).astype(np.float32)
    mix=voice+bed*envelope[:,None]
    effects=HERE/'source_audio'/'kenney'/'Audio'
    events=[(0.2,'open_001.ogg',.07),(8.,'switch_002.ogg',.10),(17.,'open_002.ogg',.09),
            (27.,'open_001.ogg',.10),(34.,'tick_001.ogg',.1),(43.,'switch_001.ogg',.08),
            (51.,'confirmation_001.ogg',.10),(57.,'open_001.ogg',.08),(64.,'tick_001.ogg',.09),
            (73.,'tick_001.ogg',.09),(82.,'switch_001.ogg',.08),(92.,'switch_001.ogg',.08),
            (102.,'open_002.ogg',.08),(115.,'tick_001.ogg',.08),(118.5,'tick_001.ogg',.08),
            (122.,'tick_001.ogg',.08),(125.5,'confirmation_001.ogg',.08)]
    for second,name,volume in events:
        decoded=narration/('sfx_'+Path(name).stem+'.wav')
        if not decoded.exists():run(['-i',effects/name,'-ar',SR,'-ac',2,'-c:a','pcm_s16le',decoded])
        effect,_=sf.read(decoded,dtype='float32',always_2d=True);peak=max(float(np.max(np.abs(effect))),.01)
        offset=round(second*SR);n=min(len(effect),len(mix)-offset);mix[offset:offset+n]+=effect[:n]*(volume/peak)
    sf.write(narration/'mix_premaster.wav',mix,SR,subtype='FLOAT')
    run(['-i',narration/'mix_premaster.wav','-af','alimiter=limit=0.8414:level=false:latency=true','-ar',SR,'-c:a','pcm_s24le',final/'ARD_final_mix.wav'])
    (narration/'timing.json').write_text(json.dumps({'duration':DURATION,'scenes':timings},ensure_ascii=False,indent=2),encoding='utf-8')
    (final/'ARD_Korean_transcript.srt').write_text('\n'.join(subtitles),encoding='utf-8-sig')
    credits='''ARD CG — audio credits

Narration: user's own locally fine-tuned myTTS XTTS-v2 voice.
No voice/model upload. Local inference, noise filtering, loudness matching,
light pitch-preserving time fitting to each cut.

"Cylinder Five" by Chris Zabriskie
From the album Cylinders (2014)
Licensed under Creative Commons Attribution 4.0
https://creativecommons.org/licenses/by/4.0/
https://chriszabriskie.com/cylinders/
https://chriszabriskie.com/use/
Source copy: https://commons.wikimedia.org/wiki/File:Chris_Zabriskie_-_05_-_Cylinder_Five.ogg
Changes: trimmed to 135 seconds, fades, narration ducking, mixed with voice/SFX.

Interface Sounds by Kenney (CC0 1.0)
https://kenney.nl/assets/interface-sounds
https://creativecommons.org/publicdomain/zero/1.0/
Changes: selected clips, level adjustment and timing.
Effects are editorial cues, not recordings of the physical machine.

Three.js: MIT. Nanum Gothic: SIL Open Font License (where used).
XTTS-v2 model: Coqui Public Model License; see myTTS README for its use terms.
'''
    (final/'CREDITS.txt').write_text(credits,encoding='utf-8')
    (final/'audio_manifest.json').write_text(json.dumps({'duration':DURATION,'sample_rate':SR,'channels':2,
        'music_sha256':hashlib.sha256(music.read_bytes()).hexdigest(),'effects':events,
        'speech':timings,'music_duck_gain':.35,'peak_limit_dbfs':-1.5},ensure_ascii=False,indent=2),encoding='utf-8')
    print('AUDIO_MIX_READY',flush=True)

if __name__=='__main__':main()
