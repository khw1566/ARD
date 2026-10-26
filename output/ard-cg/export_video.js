'use strict';
const source=document.getElementById('source'),canvas=document.getElementById('preview'),ctx=canvas.getContext('2d',{alpha:false});
const status=document.getElementById('status'),log=document.getElementById('log');
const WIDTH=1920,HEIGHT=1080,FPS=30;
let api,doc,timing=null;
async function post(route,data,headers={}){const response=await fetch('/render/'+route,{method:'POST',headers,body:data});if(!response.ok)throw new Error(await response.text());return response.json();}
function message(text){status.textContent=text;}
function logicalTime(real){if(!timing)return real;const part=timing.scenes.find(p=>real>=p.start&&real<p.end)||timing.scenes.at(-1);return part.logical_start+(real-part.start)/(part.end-part.start)*(part.logical_end-part.logical_start);}
async function overlay(){return api.overlay;}
async function frame(real){const webgl=api.frame(logicalTime(real));ctx.drawImage(webgl,0,0,WIDTH,HEIGHT);ctx.drawImage(await overlay(),0,0);ctx.getImageData(0,0,1,1);}
async function test(){try{message('시험 프레임 합성 중…');await frame(38);message('시험 프레임 정상 · 3D/자막/계기 합성 완료');}catch(error){message('오류: '+error.message);log.textContent=error.stack;}}
async function render(){
 document.getElementById('render').disabled=true;document.getElementById('test').disabled=true;
 let encoder,failure=null,chunks=[],chunkNumber=0,encoded=0;
 try{
  await frame(0);
  let config={codec:'avc1.640028',width:WIDTH,height:HEIGHT,bitrate:12000000,framerate:FPS,latencyMode:'realtime',hardwareAcceleration:'prefer-hardware',avc:{format:'annexb'}};
  let support=await VideoEncoder.isConfigSupported(config);
  if(!support.supported){config={...config,codec:'avc1.420028',hardwareAcceleration:'prefer-software'};support=await VideoEncoder.isConfigSupported(config);}
  if(!support.supported)throw new Error('H.264 WebCodecs encoder unavailable');
  const duration=timing?.duration??135,total=Math.round(duration*FPS),started=performance.now();
  await post('start',JSON.stringify({width:WIDTH,height:HEIGHT,fps:FPS,duration,frames:total,codec:config}));
  encoder=new VideoEncoder({output(chunk){const bytes=new Uint8Array(chunk.byteLength);chunk.copyTo(bytes);chunks.push(bytes);encoded++;},error(error){failure=error;}});encoder.configure(config);
  for(let i=0;i<total;i++){
   await frame(i/FPS);
   const videoFrame=new VideoFrame(canvas,{timestamp:Math.round(i*1000000/FPS),duration:Math.round(1000000/FPS)});
   encoder.encode(videoFrame,{keyFrame:i%60===0});videoFrame.close();
   if((i+1)%30===0||i+1===total){
    await encoder.flush();if(failure)throw failure;
    const bytes=new Blob(chunks,{type:'application/octet-stream'});chunks=[];
    await post('chunk',bytes,{'X-Chunk-Index':String(chunkNumber++),'X-Frame-Count':String(i+1)});
    const elapsed=(performance.now()-started)/1000;message(`프레임 ${i+1} / ${total} · ${((i+1)/total*100).toFixed(1)}% · ${elapsed.toFixed(0)}초 경과`);
    await new Promise(resolve=>setTimeout(resolve,0));
   }
  }
  encoder.close();await post('complete',JSON.stringify({frames:total,encodedFrames:encoded,duration,seconds:(performance.now()-started)/1000}));message('RENDER_COMPLETE · 영상 프레임 출력 완료');
 }catch(error){if(encoder?.state!=='closed')encoder?.close();message('오류: '+error.message);log.textContent=error.stack;await post('error',JSON.stringify({message:error.message,stack:error.stack})).catch(()=>{});}
}
source.addEventListener('load',async()=>{
 try{
  doc=source.contentDocument;api=source.contentWindow.cgExport;
  while(!api?.ready()){await new Promise(resolve=>setTimeout(resolve,100));api=source.contentWindow.cgExport;}
  const response=await fetch('/narration/timing.json');if(response.ok)timing=await response.json();
  document.getElementById('test').disabled=false;document.getElementById('render').disabled=false;message('준비 완료 · 로컬 렌더링');
 }catch(error){message('오류: '+error.message);log.textContent=error.stack;}
});
document.getElementById('test').onclick=test;document.getElementById('render').onclick=render;
