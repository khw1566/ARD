'use strict';
const source=document.getElementById('source'),canvas=document.getElementById('preview'),ctx=canvas.getContext('2d',{alpha:false});
const status=document.getElementById('status'),log=document.getElementById('log');
const WIDTH=1920,HEIGHT=1080,FPS=30,DURATION=134;let api;
async function post(route,data,headers={}){const r=await fetch('/render/'+route,{method:'POST',headers,body:data});if(!r.ok)throw Error(await r.text());return r.json();}
function frame(t){const base=t<13?api.intro(t):api.frame(t-13+8);ctx.drawImage(base,0,0,WIDTH,HEIGHT);ctx.drawImage(api.overlay,0,0,WIDTH,HEIGHT);}
document.getElementById('test').onclick=()=>{try{frame(43);status.textContent='시험 프레임 정상 · 동일한 3D/자막 합성';}catch(e){status.textContent=e.message;log.textContent=e.stack;}};
async function render(){
 document.getElementById('render').disabled=true;document.getElementById('test').disabled=true;
 let encoder,failure=null,chunks=[],number=0,encoded=0;
 try{
  frame(0);let config={codec:'avc1.640028',width:WIDTH,height:HEIGHT,bitrate:14000000,framerate:FPS,latencyMode:'realtime',hardwareAcceleration:'prefer-hardware',avc:{format:'annexb'}};
  if(!(await VideoEncoder.isConfigSupported(config)).supported)throw Error('1080p H.264 encoder unavailable');
  const total=DURATION*FPS,started=performance.now();await post('start',JSON.stringify({width:WIDTH,height:HEIGHT,fps:FPS,duration:DURATION,frames:total,codec:config,revision:'v2',introFrames:390,cgFrames:3630}));
  encoder=new VideoEncoder({output(chunk){const bytes=new Uint8Array(chunk.byteLength);chunk.copyTo(bytes);chunks.push(bytes);encoded++;},error(e){failure=e;}});encoder.configure(config);
  for(let i=0;i<total;i++){
   frame(i/FPS);const vf=new VideoFrame(canvas,{timestamp:Math.round(i*1000000/FPS),duration:Math.round(1000000/FPS)});encoder.encode(vf,{keyFrame:i%60===0});vf.close();
   if((i+1)%30===0||i+1===total){await encoder.flush();if(failure)throw failure;const blob=new Blob(chunks);chunks=[];await post('chunk',blob,{'X-Chunk-Index':String(number++),'X-Frame-Count':String(i+1)});status.textContent=`프레임 ${i+1} / ${total} · ${((i+1)/total*100).toFixed(1)}% · ${((performance.now()-started)/1000).toFixed(0)}초 경과`;await new Promise(resolve=>setTimeout(resolve,0));}
  }
  encoder.close();await post('complete',JSON.stringify({frames:total,encodedFrames:encoded,duration:DURATION,seconds:(performance.now()-started)/1000}));status.textContent='RENDER_COMPLETE · v2 출력 완료';
 }catch(e){if(encoder&&encoder.state!=='closed')encoder.close();status.textContent='오류: '+e.message;log.textContent=e.stack;await post('error',JSON.stringify({message:e.message,stack:e.stack})).catch(()=>{});}
}
source.addEventListener('load',async()=>{try{api=source.contentWindow.cgExport;while(!api?.ready()){await new Promise(resolve=>setTimeout(resolve,100));api=source.contentWindow.cgExport;}document.getElementById('test').disabled=false;document.getElementById('render').disabled=false;status.textContent='준비 완료 · GPU 렌더링';}catch(e){status.textContent=e.message;log.textContent=e.stack;}});
document.getElementById('render').onclick=render;
