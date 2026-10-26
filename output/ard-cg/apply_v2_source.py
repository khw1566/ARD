"""Apply the documented v2 presentation changes to the original CAD renderer."""
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
js=(HERE/'cinematic.js').read_text(encoding='utf-8')
assert 'const overlayCanvas=' not in js,'v2 migration already applied'
js=js.replace('stage.prepend(renderer.domElement);',"stage.prepend(renderer.domElement);\nconst overlayCanvas=document.createElement('canvas');overlayCanvas.id='artwork';overlayCanvas.width=1920;overlayCanvas.height=1080;stage.append(overlayCanvas);const artwork=overlayCanvas.getContext('2d');let captions={cg:[],intro:[]},overlayState=null,introTime=null;")
js=js.replace(" document.getElementById('av').textContent=a.toFixed(0)+'°';document.getElementById('bv').textContent=b.toFixed(0)+'°';document.getElementById('yv').textContent=yaw.toFixed(0)+'°';document.getElementById('rv').textContent=roll.toFixed(0)+'°';",'')
start=js.index('function support(frame){');end=js.index('function resize(){',start)
replacement='''function setTime(t){
 const frame=ARDShots.frameAt(t,parts.planetCenter?.toArray()),s=frame.shot,k=ease(frame.u);time=frame.time;
 active=frame.index;select.value=active;document.getElementById('seek').value=time;document.getElementById('clock').textContent=`${Math.floor(time/60)}:${String(Math.floor(time%60)).padStart(2,'0')} / 2:15`;
 if(!ready)return;
 whole.visible=!frame.isPlanet&&!frame.isMacro;macro.visible=frame.isMacro;planetary.visible=frame.isPlanet;ground.visible=whole.visible;
 parts.bins.visible=['hero','closing'].includes(s.kind);parts.cam.visible=['hero','system','closing'].includes(s.kind);parts.base.visible=true;
 cut.constant=s.kind==='section'?200-115*k:300;
 parts.upper.position.x=frame.rail||0;parts.mainRig.root.position.x=C.x+parts.upper.position.x;
 pose(frame.a,frame.b);parts.detailRig.plate.visible=frame.plateOpacity>.01;parts.detailPlateMaterial.opacity=frame.plateOpacity;parts.detailPlateMaterial.transparent=frame.plateOpacity<1;parts.detailPlateMaterial.depthWrite=frame.plateOpacity===1;
 applyCamera(frame.camera);
 if(frame.isPlanet)ARDPlanetary.pose(parts.planetRig,frame.sunTurns,frame.explode);
 if(parts.section){parts.section.visible=s.kind==='section';parts.cap.position.z=cut.constant;}
 for(const r of pivots){r.arcA.visible=r.arcB.visible=frame.isMacro&&s.kind!=='differential';}
 scene.updateMatrixWorld(true);camera.updateMatrixWorld(true);
 const labels=[];
 if(frame.isMacro&&['input-a','input-b','compound'].includes(s.kind))for(const [id,x] of [['A',-72],['B',72]]){
  const p=new THREE.Vector3(C.x+x,C.y+28,C.z).project(camera);labels.push({id,x:Math.max(84,Math.min(1215,(p.x*.5+.5)*1920-94)),y:Math.max(104,Math.min(730,(-p.y*.5+.5)*1080-60))});
 }
 const cue=(introTime===null?captions.cg:captions.intro).find(c=>(introTime??time)>=c.start&&(introTime??time)<c.end);
 overlayState={frame,labels,intro:introTime!==null,caption:cue?.text||''};
 renderer.render(scene,camera);ARDEditorial.paint(artwork,overlayState);
}
'''
js=js[:start]+replacement+js[end:]
js=js.replace("document.getElementById('sound').onclick=()=>{};",'')
js=js.replace("window.cgExport={frame(t){playing=false;setTime(t);return renderer.domElement;},stage,ready:()=>ready};","window.cgExport={frame(t){playing=false;introTime=null;setTime(t);return renderer.domElement;},intro(t){playing=false;introTime=t;setTime(Math.min(7.999,t*8/13));return renderer.domElement;},overlay:overlayCanvas,stage,data:()=>overlayState,ready:()=>ready};")
js=js.replace('async function init(){',"async function init(){\n await document.fonts.ready;await Promise.all([document.fonts.load(\"400 32px 'IBM Plex Sans KR'\"),document.fonts.load(\"500 43px 'IBM Plex Sans KR'\"),document.fonts.load(\"400 27px 'IBM Plex Mono'\")]);captions=await (await fetch('/narration/cg_captions.json')).json();")
(HERE/'cinematic.js').write_text(js,encoding='utf-8')
plan=(HERE/'cinematic_plan.js').read_text(encoding='utf-8')
plan=re.sub(r",sub:'[^']*'",'',plan)
plan=re.sub(r"kick:'[^']*'","kick:''",plan)
plan=plan.replace("position:[-690+55*k,360,-430]","position:[-690,360,-430]")
plan=plan.replace('let a=0,b=0,plateOpacity=1;','let a=0,b=0,plateOpacity=1,rail=0,step=0;')
start=plan.index("  if(kind==='sequence'){");end=plan.index("  if(kind==='differential')",start)
sequence='''  if(kind==='sequence'){
   // Each actuator finishes before the following stage starts.
   const phase=(lo,hi)=>ease((u-lo)/(hi-lo));
   rail=30*phase(0,.2)*(1-phase(.82,1));
   const yaw=40*phase(.22,.4)*(1-phase(.64,.8)),tilt=25*phase(.42,.6)*(1-phase(.64,.8));
   a=yaw+tilt;b=yaw-tilt;step=u<.22?0:u<.42?1:u<.64?2:u<.82?3:4;
  }
'''
plan=plan[:start]+sequence+plan[end:]
plan=plan.replace('return {time,index,shot,u,a,b,plateOpacity,isPlanet,isMacro,','return {time,index,shot,u,a,b,rail,step,plateOpacity,isPlanet,isMacro,')
(HERE/'cinematic_plan.js').write_text(plan,encoding='utf-8')
(HERE/'cinematic.html').write_text('''<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AI기반 자동화 분류장치 · 기구학 설명 v2</title>
<link rel="stylesheet" href="/cinematic_v2.css"><div id="stage"><div id="loading">CAD 장면을 준비하고 있습니다…</div></div>
<div class="controls"><button id="play">일시 정지</button><select id="shots" aria-label="장면 선택"></select><input type="range" id="seek" min="0" max="135" value="0" step="0.033333" aria-label="영상 위치"><span id="clock">0:00 / 2:15</span></div>
<script src="/vendor/three.min.js"></script><script src="/differential_rig.js"></script><script src="/planetary_rig.js"></script><script src="/cinematic_plan.js"></script><script src="/editorial_overlay.js"></script><script src="/cinematic.js"></script></html>
''',encoding='utf-8')
print('V2_SOURCE_APPLIED')
