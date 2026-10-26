/* Frame-driven, local CAD cinematography. This never connects to hardware. */
const stage=document.getElementById('stage'),DURATION=135,DEG=Math.PI/180;
const exportMode=new URLSearchParams(location.search).has('export');
if(exportMode)document.body.classList.add('export-mode');
const renderer=new THREE.WebGLRenderer({antialias:true,alpha:false,stencil:true,preserveDrawingBuffer:true});
renderer.setPixelRatio(exportMode?1:Math.min(devicePixelRatio,1.5));renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.05;
renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;renderer.localClippingEnabled=true;
stage.prepend(renderer.domElement);
const overlayCanvas=document.createElement('canvas');overlayCanvas.id='artwork';overlayCanvas.width=1920;overlayCanvas.height=1080;stage.append(overlayCanvas);const artwork=overlayCanvas.getContext('2d');let captions={cg:[],intro:[]},overlayState=null,introTime=null;
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
const crossMaterial=new THREE.MeshStandardMaterial({color:0xa0a990,metalness:.8,roughness:.4,roughnessMap:grain});
const planetMaterial=new THREE.MeshStandardMaterial({color:0x73b78e,metalness:.82,roughness:.4,roughnessMap:grain});
const carrierMaterial=new THREE.MeshStandardMaterial({color:0x65b9dc,metalness:.7,roughness:.35,transparent:true,opacity:.22,depthWrite:false});
const housingMaterial=new THREE.MeshStandardMaterial({color:0x8197a2,metalness:.6,roughness:.5,transparent:true,opacity:.25,depthWrite:false});
const fixedPlanetMaterial=new THREE.MeshStandardMaterial({color:0x91a1a9,metalness:.6,roughness:.48,transparent:true,opacity:.12,depthWrite:false});
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
 const nodes=ARDDifferential.create(meshes,parent,C),g=nodes.root;
 const arrow=(x,col)=>{const a=new THREE.Group();a.position.x=x;
  const arc=new THREE.Mesh(new THREE.TorusGeometry(38,.45,7,80,Math.PI*1.55),new THREE.MeshBasicMaterial({color:col}));arc.rotation.y=Math.PI/2;a.add(arc);
  const cone=new THREE.Mesh(new THREE.ConeGeometry(2.1,5.5,12),new THREE.MeshBasicMaterial({color:col}));cone.position.set(0,-37.5,-3);cone.rotation.x=-Math.PI/2;a.add(cone);g.add(a);return a;
 };
 nodes.arcA=arrow(-51,0xe7b05e);nodes.arcB=arrow(51,0x71c1e7);nodes.arcB.scale.z=-1;nodes.root=g;pivots.push(nodes);return nodes;
}
const scenes=ARDShots.scenes;
let ready=false,time=0,playing=true,last=performance.now(),active=-1;
const select=document.getElementById('shots');scenes.forEach((s,i)=>{const o=document.createElement('option');o.value=i;o.textContent=s.title;select.append(o);});
function pose(a,b){
 const yaw=(a+b)/2,roll=(a-b)/2;
 for(const r of pivots){ARDDifferential.pose(r,a,b);r.arcA.rotation.x=a*DEG;r.arcB.rotation.x=-b*DEG;}

 return {yaw,roll};
}
function applyCamera(plan){
 camera.position.fromArray(plan.position);camera.up.fromArray(plan.up);camera.fov=plan.fov;camera.lookAt(new THREE.Vector3(...plan.target));
 const w=stage.clientWidth,h=stage.clientHeight;camera.setViewOffset(w,h,w*plan.offset,0,w,h);camera.updateProjectionMatrix();
}

function makePlanetGuide(rig){
 const lineMaterial=new THREE.LineDashedMaterial({color:0x8cd4ad,dashSize:1.4,gapSize:1,transparent:true,opacity:.6});
 const points=[];for(let i=0;i<=160;i++){const a=i*Math.PI*2/160;points.push(new THREE.Vector3(24*Math.cos(a),7,24*Math.sin(a)));}
 const orbit=new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),lineMaterial);orbit.computeLineDistances();rig.root.add(orbit);
 const dot=(parent,color,x,y,z)=>{const mesh=new THREE.Mesh(new THREE.SphereGeometry(.9,10,8),new THREE.MeshBasicMaterial({color}));mesh.position.set(x,y,z);parent.add(mesh);};
 dot(rig.sun,0xffd68b,9,5.8,0);
 for(const part of rig.planets)dot(part.spin,0xe1ffe7,9,5.8,0);
 const carrierGuide=new THREE.Group();rig.carrier.add(carrierGuide);
 const blue=new THREE.LineBasicMaterial({color:0x79c9ec,transparent:true,opacity:.8});
 for(const part of rig.planets){const x=part.center.x-rig.pivot.x,z=part.center.z-rig.pivot.z;const p=[new THREE.Vector3(0,16,0),new THREE.Vector3(x,16,z)];carrierGuide.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(p),blue));}
 dot(carrierGuide,0x79c9ec,16,16,0);
 const orbitTip=new THREE.Mesh(new THREE.ConeGeometry(.95,3.4,10),new THREE.MeshBasicMaterial({color:0x79c9ec}));orbitTip.position.set(24,7,-3);orbitTip.rotation.x=-Math.PI/2;carrierGuide.add(orbitTip);
 return {orbit,carrierGuide};
}

function setTime(t){
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
function resize(){renderer.setSize(stage.clientWidth,stage.clientHeight);camera.aspect=stage.clientWidth/stage.clientHeight;setTime(time);}
addEventListener('resize',resize);document.getElementById('play').onclick=()=>{playing=!playing;document.getElementById('play').textContent=playing?'일시 정지':'재생';};
document.getElementById('seek').oninput=e=>{playing=false;document.getElementById('play').textContent='재생';setTime(+e.target.value);};
select.onchange=()=>{playing=false;document.getElementById('play').textContent='재생';const i=+select.value;setTime(scenes[i].t+((scenes[i+1]?.t??DURATION)-scenes[i].t)*.75);};

window.cgReady=false;window.cgState=()=>({time,scene:active,ready,frame:ARDShots.frameAt(time,parts.planetCenter?.toArray()),reduction:ARDPlanetary.motion(ARDShots.frameAt(time).sunTurns)});
window.cgExport={frame(t){playing=false;introTime=null;setTime(t);return renderer.domElement;},intro(t){playing=false;introTime=t;setTime(Math.min(7.999,t*8/13));return renderer.domElement;},overlay:overlayCanvas,stage,data:()=>overlayState,ready:()=>ready};
async function init(){
 await document.fonts.ready;await Promise.all([document.fonts.load("400 32px 'IBM Plex Sans KR'"),document.fonts.load("500 43px 'IBM Plex Sans KR'"),document.fonts.load("400 27px 'IBM Plex Mono'")]);captions=await (await fetch('/narration/cg_captions.json')).json();
 const specs=[['base',src('하부이송구조.stl'),silver,whole],['upper','/assets/upper_without_core.stl',polymer,whole],['cam',src('비전 캠 장치.stl'),black,whole],['bins','/assets/bins.stl',binsMat,whole]];
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
 const rigMeshes={};await Promise.all([['A',colorA,'rig_A.stl'],['B',colorB,'rig_B.stl'],['cross',crossMaterial,'rig_cross.stl'],['output',silver,'rig_outputCore.stl'],['bottom',silver,'rig_bottom.stl'],['plate',silver,'tray.stl']].map(async([id,mat,file])=>rigMeshes[id]=await load('/assets/'+file,mat)));
 parts.mainRig=rig(rigMeshes,whole);parts.detailRig=rig(rigMeshes,macro);
 parts.detailPlateMaterial=silver.clone();parts.detailRig.plate.children[0].material=parts.detailPlateMaterial;
 // An explanatory heading marker makes yaw visible on a rotationally symmetric tray.
 const heading=new THREE.Group(),headingMat=new THREE.MeshBasicMaterial({color:0xefbf6a});
 const headingLine=new THREE.Mesh(new THREE.BoxGeometry(46,.55,1.6),headingMat);headingLine.position.set(34,78,0);heading.add(headingLine);
 const headingTip=new THREE.Mesh(new THREE.ConeGeometry(2.7,7,12),headingMat);headingTip.position.set(60,78,0);headingTip.rotation.z=-Math.PI/2;heading.add(headingTip);
 parts.detailRig.plate.add(heading);
 const response=await fetch('/planetary_inventory.json');if(!response.ok)throw new Error('감속기 부품 정보 읽기 실패');const inventory=await response.json(),planetMeshes={};
 await Promise.all(inventory.parts.map(async item=>{const id=item.role,material=id==='sun'?colorA:id==='carrier'?carrierMaterial:id==='housing'?housingMaterial:id.startsWith('planet')?planetMaterial:id.startsWith('mount')?silver:fixedPlanetMaterial;planetMeshes[id]=await load('/assets/'+item.file,material);}));
 parts.planetRig=ARDPlanetary.create(planetMeshes,planetary,inventory);parts.planetCenter=parts.planetRig.pivot;parts.planetGuide=makePlanetGuide(parts.planetRig);
 document.getElementById('loading').classList.add('hidden');ready=true;window.cgReady=true;
 const initial=Number(new URLSearchParams(location.search).get('t'))||0;playing=!new URLSearchParams(location.search).has('still');document.getElementById('play').textContent=playing?'일시 정지':'재생';setTime(initial);resize();
}
function tick(now){const dt=Math.min((now-last)/1000,.1);last=now;if(ready&&playing)setTime((time+dt)%DURATION);requestAnimationFrame(tick);}
resize();requestAnimationFrame(tick);init().catch(e=>{document.getElementById('loading').textContent=e.message;console.error(e);});
