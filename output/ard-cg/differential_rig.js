/* One shared, build-free rig for the browser and the Node geometry checks. */
(function (root, factory) {
 'use strict';
 if (typeof module === 'object' && module.exports) module.exports = factory(require('./vendor/three.min.js'));
 else root.ARDDifferential = factory(root.THREE);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (THREE) {
 'use strict';
 const DEG = Math.PI / 180;

 function create(meshes, parent, pivot) {
  const root = new THREE.Group();root.name = 'differential';root.position.copy(pivot);parent.add(root);
  const carrier = new THREE.Group();carrier.name = 'cross-carrier';root.add(carrier);
  const nodes = {root, carrier};
  // All exports use the same assembly coordinates. Offset each mesh exactly
  // once; the nested joints share a local origin at the axle intersection.
  for (const id of ['A', 'B', 'cross', 'output', 'bottom', 'plate']) {
   if (!meshes[id]) throw new Error('Missing differential mesh: ' + id);
   const joint = new THREE.Group(), mesh = meshes[id].clone();joint.name = id;
   mesh.position.copy(pivot).multiplyScalar(-1);joint.add(mesh);nodes[id] = joint;
  }
  root.add(nodes.A, nodes.B);
  carrier.add(nodes.cross, nodes.output, nodes.bottom);
  nodes.output.add(nodes.plate);
  return nodes;
 }

 function pose(rig, a, b) {
  if (!Number.isFinite(a) || !Number.isFinite(b)) throw new Error('Non-finite input angle');
  const yaw = (a + b) / 2, tilt = (a - b) / 2;
  rig.A.rotation.set(a * DEG, 0, 0);
  rig.B.rotation.set(-b * DEG, 0, 0);
  // The cross is the carrier: tilt only. Upper/lower gears spin about its
  // local Y axle, independently of the cross, with opposite spin signs.
  rig.carrier.rotation.set(tilt * DEG, 0, 0);
  rig.output.rotation.set(0, yaw * DEG, 0);
  rig.bottom.rotation.set(0, -yaw * DEG, 0);
  return {a, b, yaw, tilt};
 }

 return {create, pose};
});
