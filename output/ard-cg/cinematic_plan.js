/* Editorial shots use deliberate holds, matched views and selected moves. */
(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.ARDShots=factory();})(typeof globalThis!=='undefined'?globalThis:this,function(){
 'use strict';
 const duration=135,clamp=x=>Math.max(0,Math.min(1,x)),ease=x=>{x=clamp(x);return x*x*(3-2*x);};
 const C=[-217.8659,83.9029,88.32],FULL=[-252,185,86];
 const scenes=[
  {t:0,kind:'hero',title:'AI기반 자동화 분류장치',kick:'',desc:'비전의 판단을, 분류 동작으로'},
  {t:8,kind:'system',title:'판단과 구동의 역할',kick:'',desc:'카메라 → PC 분석 → DriveMCU → 모터'},
  {t:17,kind:'section',title:'외피 아래의 전달 경로',kick:'',desc:'모터 → 유성감속기 → 차동 베벨기어'},
  {t:27,kind:'planet-parts',title:'감속기를 이루는 네 요소',kick:'',desc:'태양기어 16 · 행성기어 16 · 링기어 48'},
  {t:34,kind:'planet-roll',title:'자전하면서, 중심을 돈다',kick:'',desc:'고정된 링을 따라 이동하는 행성기어의 축'},
  {t:43,kind:'planet-count',title:'입력 네 바퀴, 출력 한 바퀴',kick:'',desc:'빠른 입력 회전 → 느린 캐리어 공전'},
  {t:51,kind:'planet-ratio',title:'같은 방향으로, 속도는 ¼',kick:'',desc:'i = 1 + 링기어 잇수 / 태양기어 잇수 = 4'},
  {t:57,kind:'differential',title:'두 입력, 두 개의 움직임',kick:'',desc:'입력 A · B / 십자 조인트 / 출력축'},
  {t:64,kind:'input-a',title:'A만 구동하면',kick:'',desc:'B = 0 / 선회 ψ = a/2 / 기울임 φ = a/2'},
  {t:73,kind:'input-b',title:'B만 구동하면',kick:'',desc:'A = 0 / 선회 ψ = b/2 / 기울임 φ = −b/2'},
  {t:82,kind:'pure-yaw',title:'같은 입력은, 선회만',kick:'',desc:'a = b → 기울임 φ = 0'},
  {t:92,kind:'pure-tilt',title:'반대 입력은, 기울임만',kick:'',desc:'a = −b → 선회 ψ = 0'},
  {t:102,kind:'compound',title:'목표 자세에서 입력을 구한다',kick:'',desc:'a = ψ + φ / b = ψ − φ'},
  {t:115,kind:'sequence',title:'분류 동작으로 연결',kick:'',desc:'이송 → 선회 → 전도 → 복귀'},
  {t:129,kind:'closing',title:'인식에서, 실제 동작까지',kick:'',desc:'AI기반 자동화 분류장치'}
 ];
 const add=(a,b)=>a.map((v,i)=>v+b[i]),mix=(a,b,t)=>a.map((v,i)=>v+(b[i]-v)*t);
 function cameraFor(kind,u,planet){
  const k=ease(u),base={up:[0,1,0],fov:30,offset:0};
  const macro=(p,target=[0,14,0],extra={})=>({...base,position:add(C,p),target:add(C,target),offset:.1,...extra});
  if(kind==='hero')return {...base,position:add(FULL,mix([-1250,780,-920],[-1080,600,-760],k)),target:FULL};
  if(kind==='system')return {...base,position:add(FULL,[-1000+70*k,490,-740]),target:FULL,offset:.1};
  if(kind==='section')return {...base,position:[-420,250,520],target:[-218,100,87],offset:.1};
  if(kind==='planet-parts')return {...base,position:add(planet,[145,155,205]),target:add(planet,[0,22,0]),offset:.13,fov:34};
  if(kind==='planet-roll'||kind==='planet-count')return {...base,position:add(planet,[0,250,0]),target:planet,up:[0,0,-1],offset:.13,fov:31};
  if(kind==='planet-ratio')return {...base,position:add(planet,[165,125,175]),target:add(planet,[0,15,0]),offset:.13,fov:33};
  if(kind==='differential')return macro([170,110,260],[0,30,0],{fov:34});
  if(kind==='input-a'||kind==='input-b')return macro([80,55,230]);
  if(kind==='pure-yaw')return macro([0,425,12],[0,35,0],{up:[0,0,-1],fov:33});
  if(kind==='pure-tilt')return macro([-140,40,365],[0,30,0],{fov:33});
  if(kind==='compound')return macro(mix([245,140,315],[210,140,340],k),[0,30,0],{fov:32});
  if(kind==='sequence')return {...base,position:[-690,360,-430],target:[-240,120,85],offset:.1,fov:34};
  return {...base,position:add(FULL,mix([-1080,600,-760],[-1210,670,-850],k)),target:FULL};
 }
 function frameAt(time,planet=[113.858,161.847,100.798]){
  time=Math.max(0,Math.min(duration,time));let index=scenes.length-1;while(index>0&&time<scenes[index].t)index--;
  const shot=scenes[index],end=scenes[index+1]?.t??duration,u=(time-shot.t)/(end-shot.t),q=ease(Math.min(1,u*1.4));
  const kind=shot.kind,isPlanet=kind.startsWith('planet-'),isMacro=['differential','input-a','input-b','pure-yaw','pure-tilt','compound'].includes(kind);
  let a=0,b=0,plateOpacity=1,rail=0,step=0;
  if(kind==='input-a'){a=60*q;plateOpacity=0;}
  if(kind==='input-b'){b=60*q;plateOpacity=0;}
  if(kind==='pure-yaw')a=b=90*q;
  if(kind==='pure-tilt'){a=34*q;b=-a;}
  if(kind==='compound'){a=65*q;b=25*q;}
  if(kind==='sequence'){
   // Each actuator finishes before the following stage starts.
   const phase=(lo,hi)=>ease((u-lo)/(hi-lo));
   rail=30*phase(0,.2)*(1-phase(.82,1));
   const yaw=40*phase(.22,.4)*(1-phase(.64,.8)),tilt=25*phase(.42,.6)*(1-phase(.64,.8));
   a=yaw+tilt;b=yaw-tilt;step=u<.22?0:u<.42?1:u<.64?2:u<.82?3:4;
  }
  if(kind==='differential')plateOpacity=.15+.85*ease((u-.5)/.5);
  return {time,index,shot,u,a,b,rail,step,plateOpacity,isPlanet,isMacro,
   sunTurns:4*clamp((time-34)/17),explode:kind==='planet-parts'?1-ease((u-.6)/.4):0,
   camera:cameraFor(kind,u,planet)};
 }
 return {duration,scenes,frameAt,cameraFor};
});
