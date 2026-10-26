/* Real CAD partition and ideal kinematics checks; no build or compiler. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const THREE=require('./vendor/three.min.js'),planetary=require('./planetary_rig.js'),shots=require('./cinematic_plan.js');
const inventory=require('./planetary_inventory.json'),source=path.resolve(__dirname,'../../영상제작용 CAD파일',inventory.source);
const crypto=require('node:crypto');
assert.equal(crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex'),inventory.source_sha256);
function read(file){const b=fs.readFileSync(file),count=b.readUInt32LE(80),positions=new Float32Array(count*9),faces=[];
 assert.equal(b.length,84+count*50);
 for(let i=0;i<count;i++){const o=84+i*50+12;faces.push(b.subarray(o,o+36).toString('hex'));for(let j=0;j<9;j++)positions[i*9+j]=b.readFloatLE(o+j*4);}
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.BufferAttribute(positions,3));return {faces,mesh:new THREE.Mesh(geometry,new THREE.MeshBasicMaterial())};}
function counts(faces){const map=new Map();for(const face of faces)map.set(face,(map.get(face)||0)+1);return [...map].sort(([a],[b])=>a.localeCompare(b));}
const meshes={},derived=[];for(const item of inventory.parts){const data=read(path.join(__dirname,'assets',item.file));meshes[item.role]=data.mesh;derived.push(...data.faces);}
assert.equal(derived.length,inventory.source_triangles);assert.deepEqual(counts(derived),counts(read(source).faces),'Original CAD coverage including repeated triangles');
const parent=new THREE.Group(),rig=planetary.create(meshes,parent,inventory),eps=(a,b)=>assert(Math.abs(a-b)<1e-10,`${a} vs ${b}`);
planetary.pose(rig,0,0);parent.updateMatrixWorld(true);
const allMeshes=[];rig.root.traverse(node=>{if(node.isMesh)allMeshes.push(node);});
for(const mesh of allMeshes){const original=new THREE.Vector3().fromBufferAttribute(mesh.geometry.attributes.position,0),world=original.clone().applyMatrix4(mesh.matrixWorld);assert(world.distanceTo(original)<1e-10,'Neutral CAD alignment');}
let cases=0;
for(const turns of [0,.125,.25,.5,1,2,3,4,-1,-4]){
 const state=planetary.pose(rig,turns,0);parent.updateMatrixWorld(true);cases++;
 eps(state.carrier,state.sun/4);eps(state.planetAbsolute,-state.sun/2);eps(state.ring,0);
 eps(16*(state.sun-state.carrier)+48*(state.ring-state.carrier),0);
 eps(16*(state.sun-state.carrier)+16*(state.planetAbsolute-state.carrier),0);
 eps(48*(state.ring-state.carrier)-16*(state.planetAbsolute-state.carrier),0);
 for(const part of rig.planets){const world=part.orbit.getWorldPosition(new THREE.Vector3()).sub(rig.pivot),initial=part.center.clone().sub(rig.pivot);eps(Math.hypot(world.x,world.z),Math.hypot(initial.x,initial.z));eps(part.spin.rotation.y+rig.carrier.rotation.y,state.planetAbsolute);}
 const stationary=rig.housing.matrixWorld.clone();eps(stationary.elements[0],1);eps(stationary.elements[5],1);eps(stationary.elements[10],1);
}
const four=planetary.motion(4);eps(four.sunTurns,4);eps(four.carrierTurns,1);eps(four.planetTurns,-2);
const starts=shots.scenes.map(s=>s.t);assert.equal(shots.duration,135);assert.equal(starts[0],0);
for(let i=1;i<starts.length;i++)assert(starts[i]>starts[i-1]);
let sampledFrames=0;for(let t=0;t<=135;t+=.25){const frame=shots.frameAt(t,inventory.pivot);sampledFrames++;assert(frame.index>=0&&frame.index<shots.scenes.length);for(const x of [...frame.camera.position,...frame.camera.target,...frame.camera.up,frame.camera.fov,frame.a,frame.b])assert(Number.isFinite(x));}
const fixed=['section','planet-parts','planet-roll','planet-count','planet-ratio','differential','input-a','input-b','pure-yaw','pure-tilt'];
for(const kind of fixed)assert.deepEqual(shots.cameraFor(kind,.1,inventory.pivot),shots.cameraFor(kind,.9,inventory.pivot),kind+' must hold its camera');
assert.deepEqual(shots.cameraFor('input-a',.5,inventory.pivot),shots.cameraFor('input-b',.5,inventory.pivot),'Matched A/B comparison');
assert.notDeepEqual(shots.cameraFor('pure-yaw',.5,inventory.pivot).position,shots.cameraFor('pure-tilt',.5,inventory.pivot).position);
assert.equal(shots.frameAt(51).sunTurns,4);assert.equal(shots.frameAt(57).isPlanet,false);
console.log(JSON.stringify({passed:true,sourceFaces:derived.length,parts:inventory.parts.length,motionCases:cases,sceneCount:shots.scenes.length,timelineSeconds:shots.duration,sampledFrames,ratio:'4 input turns / 1 output turn',planetAbsolute:'-2 turns',renderedFramesVerified:false},null,2));
