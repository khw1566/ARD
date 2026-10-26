"""Measure background camera jitter and make matched original/corrected frames."""
from pathlib import Path
import json
import cv2
import numpy as np
from PIL import Image,ImageDraw
import argparse

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];OUT=HERE/'live'
cv2.setNumThreads(4)

def motion(path):
    cap=cv2.VideoCapture(str(path));previous=None;result=[]
    # Use background storage/monitor regions; exclude device, hands and HMI.
    mask=np.zeros((360,640),np.uint8);mask[15:155,20:620]=255
    while True:
        ok,frame=cap.read()
        if not ok:break
        gray=cv2.cvtColor(cv2.resize(frame,(640,360)),cv2.COLOR_BGR2GRAY)
        if previous is not None:
            points=cv2.goodFeaturesToTrack(previous,maxCorners=160,qualityLevel=.02,minDistance=8,mask=mask)
            if points is not None:
                target,status,_=cv2.calcOpticalFlowPyrLK(previous,gray,points,None,winSize=(21,21),maxLevel=3)
                good=status.ravel()>0
                if int(good.sum())>=12:
                    transform,inliers=cv2.estimateAffinePartial2D(points[good],target[good],method=cv2.RANSAC,ransacReprojThreshold=1.5)
                    if transform is not None and int(inliers.sum())>=12:
                        center=transform[:,:2]@np.array([320.,180.])+transform[:,2]-np.array([320.,180.])
                        result.append([float(center[0])*3,float(center[1])*3,float(np.arctan2(transform[1,0],transform[0,0]))])
        previous=gray
    cap.release();data=np.asarray(result)
    smooth=np.stack([np.convolve(data[:,i],np.ones(15)/15,mode='same') for i in range(3)],axis=1)
    high=data-smooth
    path=np.cumsum(data,axis=0)
    path_smooth=np.stack([np.convolve(path[:,i],np.ones(35)/35,mode='same') for i in range(3)],axis=1)
    path_high=path-path_smooth
    return {'valid_frame_pairs':len(data),'translation_jitter_rms_px':float(np.sqrt(np.mean(np.sum(high[15:-15,:2]**2,axis=1)))),
            'angular_jitter_rms_degrees':float(np.sqrt(np.mean(high[15:-15,2]**2))*180/np.pi),
            'camera_path_jitter_rms_px':float(np.sqrt(np.mean(np.sum(path_high[35:-35,:2]**2,axis=1)))),
            'camera_path_jitter_rms_degrees':float(np.sqrt(np.mean(path_high[35:-35,2]**2))*180/np.pi)}

parser=argparse.ArgumentParser();parser.add_argument('--candidate',default='live_stabilized_clean.mp4');args=parser.parse_args()
original=ROOT/'영상제작용 CAD파일'/'20261026_195051.mp4';stable=OUT/args.candidate
before=motion(original);after=motion(stable)
report={'before':before,'after':after,'method':'LK + RANSAC on top background; 15-frame high-pass on frame-to-frame affine camera motion; 640x360 tracking rescaled to 1080p pixels',
        'translation_reduction_percent':100*(1-after['translation_jitter_rms_px']/before['translation_jitter_rms_px']),
        'rotation_reduction_percent':100*(1-after['angular_jitter_rms_degrees']/before['angular_jitter_rms_degrees']),
        'path_translation_reduction_percent':100*(1-after['camera_path_jitter_rms_px']/before['camera_path_jitter_rms_px']),
        'path_rotation_reduction_percent':100*(1-after['camera_path_jitter_rms_degrees']/before['camera_path_jitter_rms_degrees']),
        'candidate':args.candidate}
(OUT/('metrics_'+stable.stem+'.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
sheet=Image.new('RGB',(1920,4*294),'#121d23');draw=ImageDraw.Draw(sheet)
for row,second in enumerate([3,12,24,36]):
    for col,path in enumerate([original,stable]):
        cap=cv2.VideoCapture(str(path));cap.set(cv2.CAP_PROP_POS_MSEC,second*1000);ok,frame=cap.read();cap.release();assert ok
        image=Image.fromarray(cv2.cvtColor(frame,cv2.COLOR_BGR2RGB));image.thumbnail((960,270))
        sheet.paste(image,(col*960,row*294+24));draw.text((col*960+8,row*294+6),f'{"ORIGINAL" if col==0 else "STABILIZED"} / {second}s',fill='white')
sheet.save(OUT/(stable.stem+'_comparison.jpg'),quality=94)
print(json.dumps(report),flush=True)
