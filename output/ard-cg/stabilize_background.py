"""Lock the static background using a smoothed rigid camera registration.

No local warps are applied to the moving mechanism. A single rotation,
translation and uniform scale per frame correct the entire image.
"""
from pathlib import Path
import json,subprocess,time
import cv2
import numpy as np
from scipy.signal import savgol_filter
from scipy.ndimage import median_filter

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];OUT=HERE/'live'
SOURCE=ROOT/'영상제작용 CAD파일'/'20261026_195051.mp4'
FFMPEG=Path(r'C:\KHW\ffmpeg\bin\ffmpeg.exe')
cv2.setNumThreads(4)
cap=cv2.VideoCapture(str(SOURCE));ok,first=cap.read();assert ok
height,width=first.shape[:2];size=(960,540);reference=cv2.cvtColor(cv2.resize(first,size),cv2.COLOR_BGR2GRAY)
mask=np.zeros((540,960),np.uint8);mask[10:160,10:450]=255;mask[160:380,10:260]=255
points=cv2.goodFeaturesToTrack(reference,maxCorners=400,qualityLevel=.015,minDistance=9,mask=mask)
parameters=[];inlier_counts=[];frame=first
center=np.array([480.,270.])
print('BACKGROUND_REGISTRATION_ANALYSIS',flush=True)
while True:
    gray=cv2.cvtColor(cv2.resize(frame,size),cv2.COLOR_BGR2GRAY)
    moved,status,_=cv2.calcOpticalFlowPyrLK(reference,gray,points,None,winSize=(31,31),maxLevel=4)
    back,status_back,_=cv2.calcOpticalFlowPyrLK(gray,reference,moved,None,winSize=(31,31),maxLevel=4)
    good=(status.ravel()>0)&(status_back.ravel()>0)&(np.linalg.norm(back-points,axis=2).ravel()<.8)
    matrix,inliers=cv2.estimateAffinePartial2D(points[good],moved[good],method=cv2.RANSAC,ransacReprojThreshold=1.,maxIters=2000,confidence=.995)
    if matrix is None or int(inliers.sum())<25:raise ValueError(f'Insufficient static background tracks at frame {len(parameters)}')
    shift=matrix[:,:2]@center+matrix[:,2]-center
    parameters.append([shift[0]*2,shift[1]*2,np.arctan2(matrix[1,0],matrix[0,0]),np.hypot(matrix[0,0],matrix[1,0])])
    inlier_counts.append(int(inliers.sum()))
    ok,frame=cap.read()
    if not ok:break
cap.release();parameters=np.asarray(parameters)
np.save(OUT/'background_parameters.npy',parameters)
filtered=median_filter(parameters,size=(3,1),mode='nearest')
observed=savgol_filter(filtered,5,2,axis=0,mode='interp')
smoothed=savgol_filter(filtered,61,2,axis=0,mode='interp')
corrections=[];full_center=np.array([width/2,height/2])
def matrix_for(values):
    dx,dy,angle,scale=values
    rotation=np.array([[np.cos(angle),-np.sin(angle)],[np.sin(angle),np.cos(angle)]])*scale
    translation=full_center+np.array([dx,dy])-rotation@full_center
    return np.vstack([np.column_stack([rotation,translation]),[0,0,1.]])
for camera,desired in zip(observed,smoothed):
    corrections.append((matrix_for(desired)@np.linalg.inv(matrix_for(camera)))[:2])
corners=np.array([[1.,1.],[width-2.,1.],[width-2.,height-2.],[1.,height-2.]])
def enlarged(matrix,zoom):
    scale=np.array([[zoom,0,(1-zoom)*width/2],[0,zoom,(1-zoom)*height/2],[0,0,1.]])
    return (scale@np.vstack([matrix,[0,0,1]]))[:2]
zoom=1.
while zoom<1.25:
    valid=True
    for matrix in corrections:
        inverse=cv2.invertAffineTransform(enlarged(matrix,zoom));mapped=corners@inverse[:,:2].T+inverse[:,2]
        if mapped[:,0].min()<1 or mapped[:,0].max()>width-2 or mapped[:,1].min()<1 or mapped[:,1].max()>height-2:
            valid=False;break
    if valid:break
    zoom+=.001
if not valid:raise ValueError('Required crop exceeds 25 percent; manual review needed')
zoom+=.003
output=OUT/'live_stabilized_registered.mp4'
args=[str(FFMPEG),'-hide_banner','-y','-f','rawvideo','-pixel_format','bgr24','-video_size',f'{width}x{height}','-framerate','30','-i','pipe:0','-i',str(SOURCE),'-map','0:v:0','-map','1:a:0','-c:v','h264_nvenc','-preset','p6','-rc','vbr','-cq','19','-b:v','0','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-ar','48000','-movflags','+faststart',str(output)]
print(json.dumps({'status':'rendering locked background','frames':len(corrections),'static_zoom':zoom,'minimum_inliers':min(inlier_counts)}),flush=True)
with (OUT/'registered_encode.log').open('w',encoding='utf-8') as log:
    process=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT)
    cap=cv2.VideoCapture(str(SOURCE))
    for matrix in corrections:
        ok,frame=cap.read();assert ok
        corrected=cv2.warpAffine(frame,enlarged(matrix,zoom),(width,height),flags=cv2.INTER_CUBIC,borderMode=cv2.BORDER_CONSTANT)
        process.stdin.write(corrected.tobytes())
    cap.release();process.stdin.close();returncode=process.wait();assert returncode==0
np.savez_compressed(OUT/'background_registration.npz',raw=parameters,smoothed=smoothed,corrections=np.array(corrections))
report={'source':str(SOURCE.relative_to(ROOT)),'output':output.name,'frames':len(corrections),'fps':30,'static_zoom':zoom,'crop_percent_per_dimension':100*(1-1/zoom),
        'minimum_background_inliers':min(inlier_counts),'reference_mask':'Storage/books background; top-left and left static desk. Excludes moving plate, HMI and hand.',
        'transform':'Global translation, rotation and uniform scale; smooth camera path 61 frames, observation denoise 5 frames; no local deformation'}
(OUT/'background_registration.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('BACKGROUND_STABILIZATION_READY',flush=True)
