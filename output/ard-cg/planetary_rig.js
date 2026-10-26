/* Exact CAD parts, ideal ring-fixed / sun-input / carrier-output motion. */
(function(root,factory){
 if(typeof module==='object'&&module.exports)module.exports=factory(require('./vendor/three.min.js'));
 else root.ARDPlanetary=factory(root.THREE);
})(typeof globalThis!=='undefined'?globalThis:this,function(THREE){
 'use strict';
 const TAU=Math.PI*2;
 function motion(sunTurns){
  if(!Number.isFinite(sunTurns))throw new Error('Non-finite sun turns');
  const sun=sunTurns*TAU,carrier=sun/4,planetAbsolute=-sun/2;
  return {sun,ring:0,carrier,planetAbsolute,planetRelative:planetAbsolute-carrier,
          sunTurns,carrierTurns:sunTurns/4,planetTurns:-sunTurns/2};
 }
 function create(meshes,parent,inventory){
  const pivot=new THREE.Vector3(...inventory.pivot),root=new THREE.Group();root.position.copy(pivot);parent.add(root);
  const add=(id,container,origin=pivot)=>{const mesh=meshes[id].clone();mesh.position.copy(origin).multiplyScalar(-1);container.add(mesh);return mesh;};
  const sun=new THREE.Group(),carrier=new THREE.Group();root.add(sun,carrier);
  add('sun',sun);add('carrier',carrier);
  const housing=add('housing',root),fixed=add('fixed',root),planets=[];
  for(let j=0;j<4;j++){
   const center=new THREE.Vector3(...inventory.planet_centers[j]),orbit=new THREE.Group(),spin=new THREE.Group(),mount=new THREE.Group();
   orbit.position.copy(center).sub(pivot);carrier.add(orbit);orbit.add(spin,mount);
   add('planet'+j,spin,center);add('mount'+j,mount,center);planets.push({orbit,spin,mount,center});
  }
  return {root,pivot,sun,carrier,housing,fixed,planets};
 }
 function pose(rig,sunTurns,explode=0){
  const state=motion(sunTurns);rig.sun.rotation.y=state.sun;rig.carrier.rotation.y=state.carrier;
  rig.sun.position.y=-10*explode;rig.carrier.position.y=30*explode;
  for(const part of rig.planets){part.spin.rotation.y=state.planetRelative;part.orbit.position.y=part.center.y-rig.pivot.y-30*explode;}
  return state;
 }
 return {create,pose,motion};
});
