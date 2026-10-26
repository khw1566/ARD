/* Shared canvas artwork: the preview and exported movie use identical pixels. */
(function(root){
 'use strict';
 const paper='#efede7',ink='#242a2b',muted='#596364',orange='#c47f2c';
 const font=(ctx,size,mono=false,weight=400)=>ctx.font=`${weight} ${size}px '${mono?'IBM Plex Mono':'IBM Plex Sans KR'}', 'Malgun Gothic',sans-serif`;
 function text(ctx,value,x,y,size=25,color=ink,weight=400){font(ctx,size,false,weight);ctx.fillStyle=color;ctx.fillText(value,x,y);}
 function wrap(ctx,value,x,y,width,size=24,line=37,color=ink){font(ctx,size);ctx.fillStyle=color;let row='',cy=y;for(const word of value.split(' ')){const next=row?row+' '+word:word;if(ctx.measureText(next).width>width&&row){ctx.fillText(row,x,cy);row=word;cy+=line;}else row=next;}if(row)ctx.fillText(row,x,cy);return cy+line;}
 function card(ctx,x,y,w,h){ctx.fillStyle=paper;ctx.fillRect(x,y,w,h);ctx.fillStyle=orange;ctx.fillRect(x,y,4,h);}
 function caption(ctx,value,top=false){const y=top?0:976;ctx.fillStyle=paper;ctx.fillRect(0,y,1920,104);if(value){font(ctx,32);ctx.fillStyle=ink;ctx.textAlign='center';const words=value.split(' ');let lines=[''];for(const word of words){let i=lines.length-1,next=lines[i]?lines[i]+' '+word:word;if(ctx.measureText(next).width>1740&&lines[i])lines.push(word);else lines[i]=next;}const base=y+(104-lines.length*42)/2+34;lines.forEach((s,i)=>ctx.fillText(s,960,base+i*42));ctx.textAlign='left';}}
 function degree(v){return (Object.is(v,-0)?0:v).toString().replace('-','−')+'°';}
 function paint(ctx,state){
  ctx.clearRect(0,0,1920,1080);const f=state.frame,s=f.shot;
  if(state.intro){
   card(ctx,1180,270,660,420);text(ctx,'울산과학대학교',1220,330,30,muted);text(ctx,'캡스톤디자인 NEXUS',1220,385,33,ink,500);text(ctx,'팀장 김형우',1220,495,66,ink,500);wrap(ctx,'기계공학의 원리로 살펴보는 AI기반 자동화 분류장치',1220,572,560,27,41,muted);
   card(ctx,84,810,710,134);text(ctx,'AI기반 자동화 분류장치',112,866,43,ink,500);text(ctx,'NEXUS · 캡스톤디자인',112,911,25,muted);caption(ctx,state.caption);return;
  }
  const tw=Math.min(1210,Math.max(700,s.title.length*45));card(ctx,84,806,tw,138);text(ctx,s.title,112,865,43,ink,500);text(ctx,s.desc,112,912,23,muted);
  if(f.isPlanet){
   card(ctx,84,140,350,256);text(ctx,'감속기 구성',112,184,24,muted,500);
   const items=[['태양기어 16 · 입력','#a56e28'],['행성기어 16 · 자전·공전','#3d785b'],['링기어 48 · 고정','#667079'],['캐리어 · 출력','#377a96']];items.forEach(([label,c],i)=>{ctx.fillStyle=c;ctx.fillRect(112,209+i*43,10,10);text(ctx,label,140,221+i*43,23,ink);});
   card(ctx,1458,140,378,586);text(ctx,'입력과 출력의 회전',1486,187,27,ink,500);text(ctx,'링 고정 · 태양 입력',1486,230,22,muted);text(ctx,'캐리어에서 출력',1486,265,22,muted);
   text(ctx,'감속비',1486,326,23,muted);font(ctx,30,true);ctx.fillStyle=ink;ctx.fillText('1 + 48/16 = 4',1486,373);
   const turns=f.sunTurns;[turns,turns/4].forEach((v,i)=>{const x=1540+i*192,y=453;ctx.strokeStyle='#a9adab';ctx.lineWidth=2;ctx.beginPath();ctx.arc(x,y,34,0,Math.PI*2);ctx.stroke();ctx.strokeStyle=i?'#377a96':'#a56e28';ctx.lineWidth=3;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x+29*Math.sin(v*Math.PI*2),y-29*Math.cos(v*Math.PI*2));ctx.stroke();text(ctx,(i?'출력 ':'입력 ')+v.toFixed(2),x-55,521,24,ink);});
   wrap(ctx,s.kind==='planet-roll'?'행성기어의 자전과 축 중심의 공전은 서로 다른 움직임입니다.':s.kind==='planet-ratio'?'입력 네 바퀴에 출력 한 바퀴. 출력 속도는 입력의 ¼입니다.':'출력은 행성기어 자체의 자전이 아닌 캐리어의 회전입니다.',1486,585,322,23,35,muted);
  }
  if(f.isMacro&&s.kind!=='differential'){
   card(ctx,1458,180,378,470);text(ctx,'입력축과 플레이트',1486,227,27,ink,500);font(ctx,27,true);ctx.fillStyle=ink;ctx.fillText('ψ = (a + b) / 2',1486,286);ctx.fillText('φ = (a − b) / 2',1486,336);
   const a=Math.round(f.a),b=Math.round(f.b),yaw=(a+b)/2,roll=(a-b)/2;
   text(ctx,'A  '+degree(a)+'     B  '+degree(b),1486,402,27,ink);text(ctx,'선회 ψ   '+degree(yaw),1486,457,27,ink);text(ctx,'기울임 φ   '+degree(roll),1486,505,27,ink);wrap(ctx,'감속 후 입력각 · 동일 베벨기어비 가정',1486,570,320,20,30,muted);
   for(const p of state.labels||[]){card(ctx,p.x,p.y,188,48);text(ctx,'입력축 '+p.id,p.x+18,p.y+33,23,ink,500);}
  }
  const definitions={system:['판단에서 실행으로',['카메라 · 촬영 이미지','PC · AI 분류 판단','DriveMCU · 이동 명령','모터 · 기구 동작']],section:['동력이 전달되는 순서',['모터 입력축','유성감속기 · 4 : 1','차동 베벨기어','플레이트']],differential:['십자 조인트와 출력축',['A · B 입력 회전','십자 조인트 · 기울임','출력축 · 조인트 안의 선회','플레이트 · 두 동작의 조합']],sequence:['분류 동작 순서',['레일 이송','플레이트 선회','기울여 배출','중립 자세 복귀','레일 시작 위치 복귀']]};
  const block=definitions[s.kind];if(block){card(ctx,1458,150,378,s.kind==='sequence'?560:430);text(ctx,block[0],1486,199,27,ink,500);const step=f.step??Math.min(3,Math.floor(f.u*4));block[1].forEach((line,i)=>{const y=251+i*62;ctx.fillStyle=i===step?orange:'#b7bab4';ctx.fillRect(1486,y-21,6,31);text(ctx,line,1510,y+3,23,i===step?ink:muted,i===step?500:400);});
   if(s.kind==='sequence'){text(ctx,'레일 변위',1486,601,21,muted);ctx.fillStyle='#d3d4cd';ctx.fillRect(1486,623,316,5);ctx.fillStyle=orange;ctx.fillRect(1486,623,316*(f.rail||0)/30,5);text(ctx,(f.rail||0).toFixed(1)+' mm',1486,670,24,ink);}}
  caption(ctx,state.caption);
 }
 root.ARDEditorial={paint,caption};
})(window);
