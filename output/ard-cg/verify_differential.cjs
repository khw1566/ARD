/* Source-only regression checks. Runs in Node; no compiler or browser build. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const THREE = require('./vendor/three.min.js');
const differential = require('./differential_rig.js');
const DEG = Math.PI / 180;
const pivot = new THREE.Vector3(-217.8659, 83.9029, 88.32);
const rootPath = path.resolve(__dirname, '../..');

function stl(file) {
 const bytes = fs.readFileSync(file), count = bytes.readUInt32LE(80);
 assert.equal(bytes.length, 84 + count * 50, file + ': STL size');
 const positions = new Float32Array(count * 9), keys = [];
 for (let i = 0; i < count; i++) {
  const offset = 84 + i * 50 + 12;
  keys.push(bytes.subarray(offset, offset + 36).toString('hex'));
  for (let j = 0; j < 9; j++) positions[i * 9 + j] = bytes.readFloatLE(offset + j * 4);
 }
 const geometry = new THREE.BufferGeometry();geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
 return {mesh: new THREE.Mesh(geometry, new THREE.MeshBasicMaterial()), keys};
}

const assets = {A:'rig_A.stl', B:'rig_B.stl', cross:'rig_cross.stl', output:'rig_outputCore.stl', bottom:'rig_bottom.stl', plate:'tray.stl'};
const meshes = {}, partKeys = new Set(), counts = {};
for (const [id, name] of Object.entries(assets)) {
 const data = stl(path.join(__dirname, 'assets', name));meshes[id] = data.mesh;counts[id] = data.keys.length;
 if (id === 'plate') continue;
 for (const key of data.keys) {assert(!partKeys.has(key), 'Overlapping triangle: ' + id);partKeys.add(key);}
}
const assembly = stl(path.join(rootPath, '영상제작용 CAD파일', '베벨기어 조립체.stl'));
assert.equal(partKeys.size, assembly.keys.length, 'Complete assembly coverage');
for (const key of assembly.keys) assert(partKeys.has(key), 'Missing assembly triangle');
const crossSource = stl(path.join(rootPath, '영상제작용 CAD파일', '베벨기어 중심 십자조인.stl'));
const crossDerived = stl(path.join(__dirname, 'assets', 'rig_cross.stl'));
assert.deepEqual(new Set(crossDerived.keys), new Set(crossSource.keys), 'Cross must be its own exact CAD export');

const parent = new THREE.Group(), rig = differential.create(meshes, parent, pivot);
assert.equal(rig.cross.parent, rig.carrier);
assert.equal(rig.output.parent, rig.carrier);
assert.equal(rig.bottom.parent, rig.carrier);
assert.equal(rig.plate.parent, rig.output);

function matrixClose(actual, expected, label) {
 for (let i = 0; i < 16; i++) assert(Math.abs(actual.elements[i] - expected.elements[i]) < 1e-10, label + ': element ' + i);
}
const translated = new THREE.Matrix4().makeTranslation(...pivot.toArray());
const expected = (tilt, spin) => translated.clone().multiply(new THREE.Matrix4().makeRotationX(tilt * DEG)).multiply(new THREE.Matrix4().makeRotationY(spin * DEG));
const cases = [
 ['neutral',0,0,0,0],['A only',60,0,30,30],['B only',0,60,30,-30],
 ['equal / pure yaw',90,90,90,0],['opposite / pure tilt',34,-34,0,34],
 ['compound',65,25,45,20],['negative',-50,-10,-30,-20],['reset',0,0,0,0]
];
for (const [name,a,b,yaw,tilt] of cases) {
 const pose = differential.pose(rig,a,b);parent.updateMatrixWorld(true);
 assert.equal(pose.yaw,yaw);assert.equal(pose.tilt,tilt);
 matrixClose(rig.A.matrixWorld,translated.clone().multiply(new THREE.Matrix4().makeRotationX(a*DEG)),name + ' input A');
 matrixClose(rig.B.matrixWorld,translated.clone().multiply(new THREE.Matrix4().makeRotationX(-b*DEG)),name + ' input B');
 matrixClose(rig.cross.matrixWorld, expected(tilt,0), name + ' cross: no yaw');
 matrixClose(rig.output.matrixWorld, expected(tilt,yaw), name + ' upper');
 matrixClose(rig.bottom.matrixWorld, expected(tilt,-yaw), name + ' lower');
 matrixClose(rig.plate.matrixWorld, rig.output.matrixWorld, name + ' tray follows upper');
 // Every local mesh vertex must retain the CAD pose at neutral, and rotate
 // about the common axle intersection at all other poses.
 const originalVertex = new THREE.Vector3().fromBufferAttribute(meshes.cross.geometry.attributes.position,0);
 const actualVertex = originalVertex.clone().applyMatrix4(rig.cross.children[0].matrixWorld);
 const expectedVertex = originalVertex.clone().sub(pivot).applyAxisAngle(new THREE.Vector3(1,0,0),tilt*DEG).add(pivot);
 assert(actualVertex.distanceTo(expectedVertex) < 1e-10, name + ' carrier pivot');
}
// The same carrier tilt with different yaw must leave the entire cross still.
differential.pose(rig,40,0);parent.updateMatrixWorld(true);const firstCross = rig.cross.matrixWorld.clone();
differential.pose(rig,100,60);parent.updateMatrixWorld(true);
matrixClose(rig.cross.matrixWorld,firstCross,'Changing only yaw must not rotate the cross');
assert.throws(()=>differential.pose(rig,NaN,0),/Non-finite/);
console.log(JSON.stringify({assemblyTriangles:partKeys.size,counts,poseCases:cases.length,independentCrossYaw:true,passed:true},null,2));
