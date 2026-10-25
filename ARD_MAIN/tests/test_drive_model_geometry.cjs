// Real Three.js geometry/matrices; no browser, network or motor commands.
// Pass the same r160 build used by index.html as the first argument.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const THREE = require(path.resolve(process.argv[2]));
assert.equal(THREE.REVISION, '160');
const elements = new Map();
const element = id => {
    if (id === 'mini-three-container') return null;
    if (!elements.has(id)) elements.set(id, {
        value: '1', style: {}, dataset: {}, clientWidth: 1100, clientHeight: 850,
        addEventListener() {}, appendChild() {},
    });
    return elements.get(id);
};
class Renderer {
    constructor() { this.domElement = { style: {}, addEventListener() {}, setPointerCapture() {} }; }
    setPixelRatio() {} setSize() {} setClearColor() {}
    render(scene) { scene.updateMatrixWorld(true); }
}
const context = vm.createContext({
    THREE: { ...THREE, WebGLRenderer: Renderer }, console,
    window: { addEventListener() {} },
    document: { getElementById: element, querySelector: () => null, querySelectorAll: () => [] },
    requestAnimationFrame: () => 1, setTimeout() {}, performance: { now: () => 0 },
    ResizeObserver: class { observe() {} },
    fetch() { throw Error('Hardware API must not run in geometry tests'); },
});
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/simulation-drive.js'), 'utf8');
vm.runInContext(source + `
initThreeJS(); clock={getDelta:()=>1};
angleChart={data:{datasets:[{data:[]},{data:[]}]},update(){}};
window.model={scene,disk,gearX,gearY,bottomSpider,carrierGroup,yawGroup,bins:binMeshes,
    pose(yaw,roll,rail=0){window.setSortingAngles(yaw,roll,rail);animate();scene.updateMatrixWorld(true)}};
`, context);
const model = context.window.model;
const v = (x=0, y=0, z=0) => new THREE.Vector3(x,y,z);
const close = (a,b) => assert.ok(Math.abs(a-b)<1e-6, `${a} != ${b}`);
const normal = object => v(0,1,0).transformDirection(object.matrixWorld);
const origin = object => object.getWorldPosition(v());
const corners = box => [box.min.x,box.max.x].flatMap(x =>
    [box.min.y,box.max.y].flatMap(y => [box.min.z,box.max.z].map(z => v(x,y,z))));
const mounts = ['drive-mount-x','drive-mount-y'].map(name => model.scene.getObjectByName(name));

for (const gear of ['drive-bevel-x','drive-bevel-y','drive-output-bevel']) {
    const object = model.scene.getObjectByName(gear);
    assert.equal(object.userData.teeth, 24);
    for (const child of object.children) if (child.isMesh) {
        const positions = child.geometry.getAttribute('position');
        for (let i=0;i<positions.count;i++)
            assert.ok(Number.isFinite(positions.getX(i)+positions.getY(i)+positions.getZ(i)));
        assert.ok(child.geometry.getAttribute('normal'));
    }
}

// The real tray plane separates the entire mount volume at every tested tilt,
// not merely a mocked Euler value. Its swept underside also clears the base.
for (const roll of Array.from({length:37},(_,i)=>i*5-90)) for (const yaw of [0,90,180,270]) {
    model.pose(yaw,roll,15);
    const n = normal(model.disk), center = origin(model.disk);
    close(n.z,0); close(n.x,Math.sin(roll*Math.PI/180));
    close(model.carrierGroup.rotation.x,0);
    close((model.gearX.rotation.x+model.gearY.rotation.x)/2,roll*Math.PI/180);
    for (const mount of mounts) for (const corner of corners(new THREE.Box3().setFromObject(mount)))
        assert.ok(n.dot(corner.clone().sub(center)) < -0.3-0.05, `mount/tray clash at yaw=${yaw}, tilt=${roll}`);
    const positions = model.disk.geometry.getAttribute('position');
    for (let i=0;i<positions.count;i++) {
        const point = v(positions.getX(i),positions.getY(i),positions.getZ(i)).applyMatrix4(model.disk.matrixWorld);
        assert.ok(point.y > -2.8+0.05, `tray/base clash at tilt=${roll}`);
    }
    for (const name of ['drive-bevel-x','drive-bevel-y']) {
        const gear = model.scene.getObjectByName(name);
        close(Math.abs(normal(gear).z),1);
        close(normal(gear).dot(normal(model.bottomSpider)),0);
        assert.ok(origin(gear).distanceTo(origin(model.bottomSpider))<1e-6, 'bevel apex mismatch');
    }
}

// Front/back rail compartments and both dump directions. A spun round tray
// retains the same downhill edge; its top rim reaches above the open bin.
for (const rail of [-15,15]) for (const roll of [-45,45]) for (const yaw of [0,90,270]) {
    model.pose(yaw,roll,rail);
    const n = normal(model.disk);
    const downhill = v(0,-1,0).addScaledVector(n,n.y).normalize();
    const edge = origin(model.disk).addScaledVector(downhill,8.5).addScaledVector(n,0.3);
    const bin = model.bins.find(b => b.config.side===Math.sign(roll) && Math.sign(b.config.z)===Math.sign(rail));
    const box = new THREE.Box3().setFromObject(bin.group);
    assert.ok(edge.x>box.min.x && edge.x<box.max.x, `dump edge misses bin X at tilt=${roll}`);
    assert.ok(edge.z>box.min.z && edge.z<box.max.z, `dump edge misses bin Z at rail=${rail}`);
    assert.ok(edge.y>box.max.y, 'dump edge must be above open bin');
    assert.equal(bin.mat.opacity,0.75);
}
console.log('PASS real r160 geometry: 148 tilt/spin poses, mount/base clearance, shared bevel axes, 12 bin-discharge poses');
