const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const elements = new Map();
const commands = [];
function element(id) {
    if (!elements.has(id)) elements.set(id, {
        value: id.includes('gear_ratio') ? '4' : '50', dataset: {}, style: {},
        addEventListener() {}, getContext: () => ({ createLinearGradient: () => ({ addColorStop() {} }) })
    });
    return elements.get(id);
}
const context = vm.createContext({
    document: { getElementById: element, querySelector: () => null },
    window: {}, console, performance: { now: () => 0 },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {}, addLog() {},
    fetch: async (url, options) => { commands.push(JSON.parse(options.body).command); return { ok: true }; },
    THREE: { MathUtils: { lerp: (a,b,t) => a+(b-a)*t, clamp: (x,a,b) => Math.min(b,Math.max(a,x)), radToDeg: x => x*180/Math.PI } },
});
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/simulation-drive.js'), 'utf8');
vm.runInContext(source + `
clock = {getDelta:()=>1};
angleChart = {data:{datasets:[{data:[]},{data:[]}]},update(){}};
yawGroup={rotation:{y:0}}; carrierGroup={rotation:{x:0}};
window.test={tick:animate, action:runSimAction, state:()=>predictedState,
 transforms:()=>({yaw:yawGroup.rotation.y,roll:carrierGroup.rotation.x}),
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
// Positive tilt at bearing 90 discharges toward the negative-X bins.
near(Math.sin(w.test.transforms().yaw)*Math.sin(w.test.transforms().roll), -Math.SQRT1_2);
const before = Array.from(w.test.targets());
w.test.action('dr');
assert.deepEqual(Array.from(w.test.targets()), before, 'command must wait for server state');
assert.equal(commands.at(-1), 'b200,200');
w.test.action('df'); assert.equal(commands.at(-1), 'b200,-200');
w.resetSortingAngles(); w.test.tick();
near(w.test.state().yaw,0); near(w.test.state().roll,0);
console.log('Drive simulation regression checks passed');
