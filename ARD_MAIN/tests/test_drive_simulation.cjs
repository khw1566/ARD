const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const elements = new Map();
const commands = [];
function element(id) {
    if (!elements.has(id)) elements.set(id, {
        value: id.includes('gear_ratio') ? '4' : id === 'rail-steps-mm-input' ? String(1000/23) : '50', dataset: {}, style: {},
        addEventListener() {}, getContext: () => ({ createLinearGradient: () => ({ addColorStop() {} }) })
    });
    return elements.get(id);
}
const rotationFields = new Map();
const rotationMonitor = {
    querySelector(selector) {
        if (!rotationFields.has(selector)) rotationFields.set(selector, { setAttribute(name,value) { this[name]=value; } });
        return rotationFields.get(selector);
    },
    setAttribute(name,value) { this[name]=value; }
};
const context = vm.createContext({
    document: { getElementById: element, querySelector: () => null, querySelectorAll: () => [rotationMonitor] },
    window: {}, console, performance: { now: () => 0 },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {}, addLog() {},
    fetch: async (url, options) => { commands.push(JSON.parse(options.body).command); return { ok: true }; },
    THREE: { MathUtils: { lerp: (a,b,t) => a+(b-a)*t, clamp: (x,a,b) => Math.min(b,Math.max(a,x)), radToDeg: x => x*180/Math.PI } },
});
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/simulation-drive.js'), 'utf8');
vm.runInContext(source + `
let testDelta=1;
clock = {getDelta:()=>testDelta};
angleChart = {data:{datasets:[{data:[]},{data:[]}]},update(){}};
yawGroup={rotation:{y:0}}; carrierGroup={rotation:{x:0}}; carriageGroup={position:{z:0}};
window.test={tick:animate, action:runSimAction, state:()=>predictedState,
 delta:value=>testDelta=value,
 transforms:()=>({yaw:yawGroup.rotation.y,roll:carrierGroup.rotation.x,rail:carriageGroup.position.z}),
 targets:()=>[targetAngleX,targetAngleY]};`, context);
const w = context.window;
function near(actual, expected) { assert.ok(Math.abs(actual-expected)<1e-9, `${actual} != ${expected}`); }
for (const [mx,my,yaw,roll] of [[90,90,90,0],[90,-90,0,90],[-90,-90,-90,0],[135,45,90,45]]) {
    w.currentMotorState={mx_deg:mx,my_deg:my,mz_deg:0};
    w.syncDriveSim(); w.test.tick();
    near(w.test.state().yaw,yaw); near(w.test.state().roll,roll);
}
w.setSortingAngles(90,45,0); w.test.tick();
near(w.test.state().yaw,90); near(w.test.state().roll,45);
near(w.test.transforms().yaw,-Math.PI/2); near(w.test.transforms().roll,Math.PI/4);
const before = Array.from(w.test.targets());
w.test.action('dr');
assert.deepEqual(Array.from(w.test.targets()), before, 'command must wait for server state');
assert.equal(commands.at(-1), 'b200,200');
w.test.action('df'); assert.equal(commands.at(-1), 'b200,-200');
w.resetSortingAngles(); w.test.tick();
near(w.test.state().yaw,0); near(w.test.state().roll,0);

// A pure CW / CCW command preserves existing tilt and rail position, on every frame.
element('gear_ratio_z').value='1';
for (const direction of [1,-1]) {
    w.setSortingAngles(0,30,15); w.test.tick();
    const railBefore=w.test.transforms().rail;
    w.currentMotorState={mx_deg:30+direction*360,my_deg:-30+direction*360,
        mz_steps:15*(1000/23),mz_deg:15*(1000/23)*1.8};
    w.syncDriveSim(); w.test.delta(.016);
    for(let i=0;i<180;i++) {
        w.test.tick();
        near(w.test.state().roll,30);
        near(w.test.transforms().rail,railBefore);
    }
    w.test.delta(1); w.test.tick();
    near(w.test.state().yaw,direction*360);
    assert.equal(rotationFields.get('[data-disk-total]').textContent,`${direction*360}.0°`);
    assert.equal(rotationFields.get('[data-disk-pointer]').transform,`rotate(${direction*360} 50 50)`);
}
// Missing axes in sensor-only messages preserve the last commanded pose.
const preserved={...w.test.state()};
element('rail-steps-mm-input').value='100'; // An unapplied form edit must not move the rail.
w.syncDriveSim(); w.test.tick();
near(w.test.state().rail_mm,preserved.rail_mm);
w.currentMotorState={tof_dist:160}; w.syncDriveSim(); w.test.tick();
near(w.test.state().yaw,preserved.yaw); near(w.test.state().roll,preserved.roll);
near(w.test.state().rail_mm,preserved.rail_mm);

// Check motion direction during animation, including reverse and multiple turns.
w.resetSortingAngles(); w.test.tick(); w.test.delta(.016);
w.setSortingAngles(720,0,0); w.test.tick();
assert.equal(rotationFields.get('[data-disk-motion]').textContent,'시계 방향 (CW)');
near(w.test.state().roll,0);
w.test.delta(1); w.test.tick();
w.setSortingAngles(-90,0,0); w.test.delta(.016); w.test.tick();
assert.equal(rotationFields.get('[data-disk-motion]').textContent,'반시계 방향 (CCW)');
w.test.delta(1); w.test.tick();
assert.equal(rotationFields.get('[data-disk-bearing]').textContent,'270.0°');

element('gear_ratio_y').value='2';
w.test.action('dr'); assert.equal(commands.at(-1),'b200,100');
w.test.action('dl'); assert.equal(commands.at(-1),'b-200,-100');
console.log('Drive simulation regression checks passed');
