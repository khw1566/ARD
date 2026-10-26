/* Frame-driven, local CAD cinematography. This never connects to hardware. */
const stage=document.getElementById('stage'),DURATION=135,DEG=Math.PI/180;
const renderer=new THREE.WebGLRenderer({antialias:true,alpha:false,stencil:true,preserveDrawingBuffer:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,1.5));renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.05;
renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;renderer.localClippingEnabled=true;
stage.prepend(renderer.domElement);
const scene=new THREE.Scene(),camera=new THREE.PerspectiveCamera(30,1,1,8000);
scene.fog=new THREE.Fog(0x28333c,1800,4500);
const C=new THREE.Vector3(-217.8659,83.9029,88.32),FULL=new THREE.Vector3(-252,185,86);
const whole=new THREE.Group(),macro=new THREE.Group(),planetary=new THREE.Group();scene.add(whole,macro,planetary);
const clamp=x=>Math.max(0,Math.min(1,x)),ease=x=>{x=clamp(x);return x*x*(3-2*x);};
let seed=731;const rand=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
function texture(type){
 const c=document.createElement('canvas');c.width=c.height=512;const x=c.getContext('2d'),im=x.createImageData(512,512);
 for(let y=0;y<512;y++){const stripe=type==='metal'?(rand()-.5)*16:0;for(let j=0;j<512;j++){
  const p=(y*512+j)*4,v=type==='metal'?171+stripe+(rand()-.5)*14:151+12*Math.cos(y*Math.PI*2/9)+(rand()-.5)*22;
  im.data[p]=im.data[p+1]=im.data[p+2]=v;im.data[p+3]=255;
 }}x.putImageData(im,0,0);const t=new THREE.CanvasTexture(c);t.wrapS=t.wrapT=THREE.RepeatWrapping;t.repeat.set(type==='metal'?4:3,type==='metal'?4:3);return t;
}
const grain=texture('metal'),resin=texture('resin');
function env(){
 const c=document.createElement('canvas');c.width=2048;c.height=1024;const x=c.getContext('2d');
 const grad=x.createLinearGradient(0,0,0,1024);grad.addColorStop(0,'#7d8b99');grad.addColorStop(.48,'#3c474e');grad.addColorStop(.6,'#121a21');grad.addColorStop(1,'#40474b');x.fillStyle=grad;x.fillRect(0,0,2048,1024);
 x.filter='blur(30px)';x.fillStyle='#e6e9e6';x.fillRect(180,190,270,360);x.fillStyle='#b5cdd8';x.fillRect(1260,160,380,340);x.fillStyle='#ddbe8c';x.fillRect(790,220,95,260);
 const t=new THREE.CanvasTexture(c);t.mapping=THREE.EquirectangularReflectionMapping;t.colorSpace=THREE.SRGBColorSpace;
 const pm=new THREE.PMREMGenerator(renderer);scene.environment=pm.fromEquirectangular(t).texture;t.dispose();pm.dispose();
 const bg=document.createElement('canvas');bg.width=16;bg.height=512;const q=bg.getContext('2d'),g=q.createLinearGradient(0,0,0,512);
 g.addColorStop(0,'#121a22');g.addColorStop(.48,'#34414a');g.addColorStop(1,'#252c31');q.fillStyle=g;q.fillRect(0,0,16,512);const bgTexture=new THREE.CanvasTexture(bg);bgTexture.colorSpace=THREE.SRGBColorSpace;scene.background=bgTexture;
}env();
const silver=new THREE.MeshStandardMaterial({color:0xb5c1c7,metalness:1,roughness:.48,roughnessMap:grain,bumpMap:grain,bumpScale:.035,envMapIntensity:1.5});
const polymer=new THREE.MeshStandardMaterial({color:0x4e5a64,metalness:0,roughness:.63,bumpMap:resin,bumpScale:.045});
const black=new THREE.MeshStandardMaterial({color:0x252c33,metalness:0,roughness:.46,bumpMap:resin,bumpScale:.03});
const colorA=new THREE.MeshStandardMaterial({color:0xd69b48,metalness:1,roughness:.32,roughnessMap:grain});
const colorB=new THREE.MeshStandardMaterial({color:0x387ba2,metalness:1,roughness:.32,roughnessMap:grain});
const binsMat=new THREE.MeshStandardMaterial({color:0x87a0ad,metalness:0,roughness:.28,transparent:true,opacity:.08,depthWrite:false,side:THREE.DoubleSide});
const cut=new THREE.Plane(new THREE.Vector3(0,0,-1),200);polymer.clippingPlanes=[cut];
scene.add(new THREE.HemisphereLight(0xcedee8,0x27323a,1.25));
const key=new THREE.DirectionalLight(0xffe4c1,3.0);key.position.set(-650,650,650);key.target.position.copy(C);scene.add(key,key.target);
key.castShadow=true;key.shadow.mapSize.set(2048,2048);key.shadow.normalBias=.8;key.shadow.bias=-.0002;key.shadow.radius=4;
Object.assign(key.shadow.camera,{left:-650,right:650,top:650,bottom:-650,near:10,far:2500});
const rim=new THREE.DirectionalLight(0x9bc9ee,2);rim.position.set(300,300,-400);scene.add(rim);
const soft=new THREE.DirectionalLight(0xffffff,.8);soft.position.set(-200,300,600);scene.add(soft);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(12000,12000),new THREE.MeshStandardMaterial({color:0x303940,roughness:.9,metalness:0,bumpMap:resin,bumpScale:.1}));
ground.rotation.x=-Math.PI/2;ground.position.y=-29;ground.receiveShadow=true;scene.add(ground);
const parts={},pivots=[];
async function load(url,mat){
 const r=await fetch(url);if(!r.ok)throw Error('CAD 읽기 실패: '+url);const b=await r.arrayBuffer(),d=new DataView(b),n=d.getUint32(80,true);
 if(b.byteLength!==84+50*n)throw Error('STL 크기 오류');const p=new Float32Array(n*9),normal=new Float32Array(n*9);
 for(let i=0;i<n;i++){const o=84+i*50;for(let v=0;v<3;v++)for(let a=0;a<3;a++){p[i*9+v*3+a]=d.getFloat32(o+12+12*v+4*a,true);normal[i*9+v*3+a]=d.getFloat32(o+4*a,true);}}
 // Smooth curved CAD facets without blending across sharp tooth and rim edges.
 const neighbours=new Map(),keys=new Array(n*3),uv=new Float32Array(n*6);
 for(let v=0;v<n*3;v++){
  const j=v*3,key=[Math.round(p[j]*1000),Math.round(p[j+1]*1000),Math.round(p[j+2]*1000)].join(',');keys[v]=key;
  if(!neighbours.has(key))neighbours.set(key,[]);neighbours.get(key).push(j);
  const nx=Math.abs(normal[j]),ny=Math.abs(normal[j+1]),nz=Math.abs(normal[j+2]);
  uv[v*2]=(nx>ny&&nx>nz?p[j+2]:p[j])/35;uv[v*2+1]=(ny>nx&&ny>nz?p[j+2]:p[j+1])/35;
 }
 const smooth=new Float32Array(normal.length),cosCrease=Math.cos(28*DEG);
 for(let v=0;v<n*3;v++){
  const j=v*3;let x=0,y=0,z=0;for(const a of neighbours.get(keys[v])){
   if(normal[j]*normal[a]+normal[j+1]*normal[a+1]+normal[j+2]*normal[a+2]>=cosCrease){x+=normal[a];y+=normal[a+1];z+=normal[a+2];}
  }const len=Math.hypot(x,y,z)||1;smooth[j]=x/len;smooth[j+1]=y/len;smooth[j+2]=z/len;
 }
 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.BufferAttribute(p,3));g.setAttribute('normal',new THREE.BufferAttribute(smooth,3));g.setAttribute('uv',new THREE.BufferAttribute(uv,2));g.computeBoundingBox();
 const m=new THREE.Mesh(g,mat);m.castShadow=m.receiveShadow=true;return m;
}
const src=f=>'/cad/'+encodeURIComponent(f);
function rig(meshes,parent){
 const g=new THREE.Group();g.position.copy(C);parent.add(g);
 const nodes={};for(const [id,source] of Object.entries(meshes)){
  const joint=new THREE.Group(),m=source.clone();m.position.copy(C).multiplyScalar(-1);joint.add(m);
  if(id==='plate'){nodes.output.add(joint);}else g.add(joint);nodes[id]=joint;
 }
 const arrow=(x,col)=>{const a=new THREE.Group();a.position.x=x;
  const arc=new THREE.Mesh(new THREE.TorusGeometry(38,.45,7,80,Math.PI*1.55),new THREE.MeshBasicMaterial({color:col}));arc.rotation.y=Math.PI/2;a.add(arc);
  const cone=new THREE.Mesh(new THREE.ConeGeometry(2.1,5.5,12),new THREE.MeshBasicMaterial({color:col}));cone.position.set(0,-37.5,-3);cone.rotation.x=-Math.PI/2;a.add(cone);g.add(a);return a;
 };
 nodes.arcA=arrow(-51,0xe7b05e);nodes.arcB=arrow(51,0x71c1e7);nodes.arcB.scale.z=-1;nodes.root=g;pivots.push(nodes);return nodes;
}
const scenes=[
 {t:0,title:'AI기반 자동화 분류장치',kick:'VISION TO MOTION',desc:'촬영한 물체를 분석하고, 기구의 동작으로 분류합니다.',sub:'비전의 판단은 어떻게 플레이트의 움직임이 될까요?'},
 {t:10,title:'판단을 움직임으로',kick:'01 / SYSTEM',desc:'비전 카메라 → PC 분석 → DriveMCU → 모터',sub:'분류 결과에 따라 이동할 위치와 플레이트의 자세를 정합니다.'},
 {t:21,title:'외피 아래의 구동 구조',kick:'02 / SECTION',desc:'외피 단면으로 드러나는 감속기와 차동 기구',sub:'두 모터의 회전은 감속기를 거쳐 서로 마주 보는 베벨기어로 전달됩니다.'},
 {t:34,title:'감속기의 내부',kick:'03 / REDUCTION',desc:'회전을 전달하는 유성기어와 출력축',sub:'감속기는 모터 입력과 베벨기어 입력축 사이에 배치됩니다.'},
 {t:44,title:'두 입력, 두 개의 움직임',kick:'04 / DIFFERENTIAL',desc:'A · B 입력축 / 출력축 / 플레이트',sub:'플레이트는 자신의 축을 중심으로 선회하고, 입력축을 중심으로 기울어집니다.'},
 {t:55,title:'모터 A만 구동하면',kick:'05 / INPUT A',desc:'B는 고정하고 A의 입력만 변화',sub:'한쪽 입력만 바꾸면 선회와 기울임이 함께 발생합니다.'},
 {t:68,title:'모터 B만 구동하면',kick:'06 / INPUT B',desc:'A는 고정하고 B의 입력만 변화',sub:'다른 쪽 입력은 선회에 더해지고, 기울임에는 반대 방향으로 작용합니다.'},
 {t:81,title:'입력을 같게 하면',kick:'07 / PURE YAW',desc:'a = b → φ = 0',sub:'같은 크기의 정규화된 입력은 기울임을 상쇄하고 선회만 남깁니다.'},
 {t:95,title:'입력을 반대로 하면',kick:'08 / PURE TILT',desc:'a = −b → ψ = 0',sub:'반대 크기의 입력은 선회를 상쇄하고 플레이트를 기울입니다.'},
 {t:109,title:'두 움직임을 조합하면',kick:'09 / KINEMATICS',desc:'a = ψ + φ / b = ψ − φ',sub:'목표 선회각과 기울임각을 더하고 빼면 두 입력축의 목표각을 구할 수 있습니다.'},
 {t:121,title:'레일 이동과 분류 동작',kick:'10 / SEQUENCE',desc:'이송 → 선회 → 전도 → 복귀',sub:'실제 선별에서는 레일과 선회, 전도를 순서대로 실행합니다.'},
 {t:130,title:'인식에서, 실제 동작까지',kick:'ARD',desc:'AI기반 자동화 분류장치',sub:'이제 실제 장치의 분류 과정을 확인합니다.'}
];
let ready=false,time=0,playing=true,last=performance.now(),active=-1;
const select=document.getElementById('shots');scenes.forEach((s,i)=>{const o=document.createElement('option');o.value=i;o.textContent=s.title;select.append(o);});
function pose(a,b){
 const yaw=(a+b)/2,roll=(a-b)/2;
 for(const r of pivots){r.A.rotation.x=a*DEG;r.B.rotation.x=-b*DEG;r.output.rotation.set(roll*DEG,yaw*DEG,0,'XYZ');r.bottom.rotation.set(roll*DEG,-yaw*DEG,0,'XYZ');r.arcA.rotation.x=a*DEG;r.arcB.rotation.x=-b*DEG;}
 document.getElementById('av').textContent=a.toFixed(0)+'°';document.getElementById('bv').textContent=b.toFixed(0)+'°';document.getElementById('yv').textContent=yaw.toFixed(0)+'°';document.getElementById('rv').textContent=roll.toFixed(0)+'°';
 return {yaw,roll};
}
function cameraAt(target,theta,radius,elevation,offset=0){
 camera.position.set(target.x+Math.cos(theta)*radius,target.y+elevation,target.z+Math.sin(theta)*radius);camera.lookAt(target);
 const w=stage.clientWidth,h=stage.clientHeight;camera.setViewOffset(w,h,w*offset,0,w,h);camera.updateProjectionMatrix();
}
function labels(show){
 for(const [id,x,color] of [['A',-47,'#efb45c'],['B',47,'#79c9ec']]){
  const l=document.getElementById('label'+id),path=document.getElementById('line'+id);l.style.opacity=show?1:0;path.style.display=show?'':'none';if(!show)continue;
  const p=new THREE.Vector3(C.x+x,C.y,C.z).project(camera),w=stage.clientWidth,h=stage.clientHeight;
  const lx=id==='A'?.10*w:.59*w,ly=.18*h+18;
  path.setAttribute('d',`M ${lx+30} ${ly} L ${lx+30} ${ly+24} L ${(p.x*.5+.5)*w} ${(-p.y*.5+.5)*h}`);
 }
}
function setTime(t){
 time=Math.max(0,Math.min(DURATION,t));let i=scenes.length-1;while(i>0&&time<scenes[i].t)i--;
 const s=scenes[i],end=scenes[i+1]?.t??DURATION,u=(time-s.t)/(end-s.t),k=ease(u);
 if(active!==i){document.getElementById('kicker').textContent=s.kick;document.getElementById('headline').textContent=s.title;document.getElementById('description').textContent=s.desc;document.getElementById('subtitle').textContent=s.sub;document.getElementById('chapter').textContent=String(i+1).padStart(2,'0')+' / '+scenes.length;select.value=i;active=i;}
 document.getElementById('seek').value=time;document.getElementById('clock').textContent=`${Math.floor(time/60)}:${String(Math.floor(time%60)).padStart(2,'0')} / 2:15`;document.getElementById('progress').style.width=time/DURATION*100+'%';
 if(!ready)return;
 whole.visible=i<3||i>=10;macro.visible=i>=4&&i<=9;planetary.visible=i===3;ground.visible=whole.visible;
 parts.bins.visible=i===0||i===11;parts.cam.visible=i<2||i===11;parts.base.visible=true;
 cut.constant=i===2?200-115*k:300;
 parts.upper.position.x=i===10?15*Math.sin(k*Math.PI):0;parts.mainRig.root.position.x=C.x+parts.upper.position.x;
 let a=0,b=0;
 if(i===5){a=60*ease(Math.min(1,u*1.4));}
 if(i===6){b=60*ease(Math.min(1,u*1.4));}
 if(i===7){a=b=90*ease(Math.min(1,u*1.4));}
 if(i===8){a=34*ease(u*2.5)*(1-ease((u-.82)/.18));b=-a;}
 if(i===9){const q=ease(Math.min(1,u*1.4));a=65*q;b=25*q;}
 pose(a,b);
 const mathOn=i>=5&&i<=9;document.getElementById('math').style.opacity=mathOn?1:0;
 document.querySelector('.editorial').style.maxWidth=i>=4&&i<=9?'62%':'50%';
 if(i<2||i===11){cameraAt(FULL,-2.55+k*.17,1350,600,0);}
 else if(i===2){cameraAt(new THREE.Vector3(-218,100,87),2.1-k*.25,480,150,0);}
 else if(i===3){cameraAt(parts.planetCenter,.6+k*.25,260,145,0);}
 else if(i>=4&&i<=9){
  const pullback=i>=5?ease((u-.5)/.35):1;
  cameraAt(new THREE.Vector3(C.x,C.y+40*pullback,C.z),i===5?2.22:(i===6?.8:1.25)+k*.07,205+190*pullback+(i===8?15:0),45+65*pullback,.08);
  const plateOpacity=i===4?1:ease((pullback-.4)/.4);
  parts.detailRig.plate.visible=plateOpacity>.01;
  parts.detailPlateMaterial.opacity=plateOpacity;parts.detailPlateMaterial.transparent=plateOpacity<1;parts.detailPlateMaterial.depthWrite=plateOpacity===1;
 }
 else{cameraAt(new THREE.Vector3(-240,100,85),-2.25+k*.12,700,260,0);}
 if(parts.section){parts.section.visible=i===2;parts.cap.position.z=cut.constant;}
 for(const r of pivots){r.arcA.visible=r.arcB.visible=macro.visible&&i>=5;}
 labels(macro.visible&&i>=4&&u<.65);renderer.render(scene,camera);
}
function resize(){renderer.setSize(stage.clientWidth,stage.clientHeight);camera.aspect=stage.clientWidth/stage.clientHeight;setTime(time);}
addEventListener('resize',resize);document.getElementById('play').onclick=()=>{playing=!playing;document.getElementById('play').textContent=playing?'일시 정지':'재생';};
document.getElementById('seek').oninput=e=>{playing=false;document.getElementById('play').textContent='재생';setTime(+e.target.value);};
select.onchange=()=>{playing=false;document.getElementById('play').textContent='재생';const i=+select.value;setTime(scenes[i].t+((scenes[i+1]?.t??DURATION)-scenes[i].t)*.75);};
document.getElementById('sound').onclick=()=>{};
window.cgReady=false;window.cgState=()=>({time,scene:active,ready,materials:'procedural brushed metal / resin',pose:poseValues(time)});
function poseValues(t){return {note:'Input signs normalized to existing PC command convention; reduced shaft angles.'};}
async function init(){
 const specs=[['base',src('하부이송구조.stl'),silver,whole],['upper','/assets/upper_without_core.stl',polymer,whole],['cam',src('비전 캠 장치.stl'),black,whole],['bins','/assets/bins.stl',binsMat,whole],['planet',src('커버와 베어링이 제거된 감속기.stl'),silver,planetary]];
 await Promise.all(specs.map(async([id,url,mat,parent])=>{const m=await load(url,mat);parts[id]=m;parent.add(m);}));
 // Stencil capping fills the sliced solid surfaces and hatches the section.
 const section=new THREE.Group();whole.add(section);parts.section=section;
 for(const [side,operation,order] of [[THREE.BackSide,THREE.IncrementWrapStencilOp,1],[THREE.FrontSide,THREE.DecrementWrapStencilOp,2]]){
  const mat=new THREE.MeshBasicMaterial({side,depthWrite:false,depthTest:false,colorWrite:false,clippingPlanes:[cut],stencilWrite:true,stencilFunc:THREE.AlwaysStencilFunc,stencilFail:operation,stencilZFail:operation,stencilZPass:operation});
  const mesh=new THREE.Mesh(parts.upper.geometry,mat);mesh.renderOrder=order;section.add(mesh);
 }
 const hatchCanvas=document.createElement('canvas');hatchCanvas.width=hatchCanvas.height=64;const hatchCtx=hatchCanvas.getContext('2d');hatchCtx.fillStyle='#b8955e';hatchCtx.fillRect(0,0,64,64);hatchCtx.strokeStyle='#66553c';hatchCtx.lineWidth=3;
 for(let q=-64;q<128;q+=12){hatchCtx.beginPath();hatchCtx.moveTo(q,0);hatchCtx.lineTo(q+64,64);hatchCtx.stroke();}
 const hatch=new THREE.CanvasTexture(hatchCanvas);hatch.wrapS=hatch.wrapT=THREE.RepeatWrapping;hatch.repeat.set(16,8);hatch.colorSpace=THREE.SRGBColorSpace;
 const capMat=new THREE.MeshBasicMaterial({map:hatch,side:THREE.DoubleSide,stencilWrite:true,stencilRef:0,stencilFunc:THREE.NotEqualStencilFunc,stencilFail:THREE.ReplaceStencilOp,stencilZFail:THREE.ReplaceStencilOp,stencilZPass:THREE.ReplaceStencilOp});
 const cap=new THREE.Mesh(new THREE.PlaneGeometry(400,220),capMat);cap.position.set(-218,80,cut.constant);cap.renderOrder=3;cap.onAfterRender=()=>renderer.clearStencil();section.add(cap);parts.cap=cap;
 const rigMeshes={};await Promise.all([['A',colorA,'rig_A.stl'],['B',colorB,'rig_B.stl'],['output',silver,'rig_outputCore.stl'],['bottom',silver,'rig_bottom.stl'],['plate',silver,'tray.stl']].map(async([id,mat,file])=>rigMeshes[id]=await load('/assets/'+file,mat)));
 parts.mainRig=rig(rigMeshes,whole);parts.detailRig=rig(rigMeshes,macro);
 parts.detailPlateMaterial=silver.clone();parts.detailRig.plate.children[0].material=parts.detailPlateMaterial;
 // An explanatory heading marker makes yaw visible on a rotationally symmetric tray.
 const heading=new THREE.Group(),headingMat=new THREE.MeshBasicMaterial({color:0xefbf6a});
 const headingLine=new THREE.Mesh(new THREE.BoxGeometry(46,.55,1.6),headingMat);headingLine.position.set(34,78,0);heading.add(headingLine);
 const headingTip=new THREE.Mesh(new THREE.ConeGeometry(2.7,7,12),headingMat);headingTip.position.set(60,78,0);headingTip.rotation.z=-Math.PI/2;heading.add(headingTip);
 parts.detailRig.plate.add(heading);
 const bb=parts.planet.geometry.boundingBox;parts.planetCenter=bb.getCenter(new THREE.Vector3());
 document.getElementById('loading').classList.add('hidden');ready=true;window.cgReady=true;
 const initial=Number(new URLSearchParams(location.search).get('t'))||0;playing=!new URLSearchParams(location.search).has('still');document.getElementById('play').textContent=playing?'일시 정지':'재생';setTime(initial);resize();
}
function tick(now){const dt=Math.min((now-last)/1000,.1);last=now;if(ready&&playing)setTime((time+dt)%DURATION);requestAnimationFrame(tick);}
resize();requestAnimationFrame(tick);init().catch(e=>{document.getElementById('loading').textContent=e.message;console.error(e);});
