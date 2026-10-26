const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/main.js'), 'utf8');
const from = source.indexOf('    function handleSortingUpdate(status) {');
const to = source.indexOf('    window.handleSortingUpdate =', from);
const elements = new Map();
function getElementById(id) {
    if (!elements.has(id)) elements.set(id, {
        style: {}, classes: new Set(), classList: {
            remove(...names) { names.forEach(n => elements.get(id).classes.delete(n)); },
            add(name) { elements.get(id).classes.add(name); }
        }
    });
    return elements.get(id);
}
const poses = [];
const context = vm.createContext({ document: { getElementById },
    window: { setSortingAngles: (...pose) => poses.push(pose) } });
vm.runInContext(source.slice(from, to), context);
const update = status => { context.status=status; vm.runInContext('handleSortingUpdate(status)',context); };
update({step:'rail_moving', step_num:4, is_running:true, target_yaw:0, rail_slot:'plus',
    dump_roll:-45, target_rail_z:15, current_yaw:0, current_roll:0, current_rail_z:15});
assert.deepEqual(poses.at(-1),[0,0,15]);
assert.equal(getElementById('sorting-3d-dest').textContent,'레일 +측 · 후진 전도');
update({step:'dumping', step_num:5, is_running:true, rail_slot:'plus', dump_roll:-45, current_yaw:90, current_roll:-45, current_rail_z:15});
assert.deepEqual(poses.at(-1),[0,-45,15],'backward tilt animates negative roll, never a disk turn');
update({step:'rail_moving', step_num:4, is_running:true, rail_slot:'plus', dump_roll:-45, current_roll:0, current_rail_z:15});
assert.equal(getElementById('sorting-reset-label').textContent,'선별 중단');
assert.ok(getElementById('pstep-4').classes.has('active'));
assert.ok(getElementById('pstep-3').classes.has('completed'));
assert.ok(getElementById('pstep-1').classes.has('completed'), 'step 1 initialize done');
assert.equal(getElementById('pstep-4-state').textContent,'▶ 진행 중');
assert.match(getElementById('sorting-now-step').textContent,/STEP 4\/6 · 레일 이송 진행 중/);
assert.equal(getElementById('sorting-now').className,'sorting-now state-running');
update({step:'error',step_num:5,is_running:false,error:'DriveMCU rejected'});
assert.ok(getElementById('pstep-5').classes.has('failed'));
assert.match(getElementById('sorting-now-desc').textContent,/DriveMCU rejected/);
update({step:'error',is_running:false,step_desc:'MCU disconnected'});
assert.equal(poses.length,3,"error updates never move the 3D model");
assert.equal(getElementById('sorting-status-badge').textContent,'ERROR');
assert.equal(getElementById('sorting-start-btn').disabled,false);
update({step:'error',is_running:false,retry_ready:false,step_desc:'Reset needed'});
assert.equal(getElementById('sorting-start-btn').disabled,true);
assert.equal(getElementById('sorting-reset-label').textContent,'초기화 / 재시도 준비');
update({step:'error',is_running:false,retry_ready:true,step_desc:'Camera recovered'});
assert.equal(getElementById('sorting-start-btn').disabled,false);
update({step:'complete',step_num:6,is_running:false});
for(let i=1;i<=6;i++) assert.ok(getElementById(`pstep-${i}`).classes.has('completed'));
context.window.currentMotorState={mx_deg:120,my_deg:60,mz_steps:650};
const countBeforeIdle=poses.length;
update({step:'idle',is_running:false,current_yaw:0,current_roll:0,current_rail_z:0});
assert.equal(poses.length,countBeforeIdle,'idle sorting status must not reset manual axes');
for(let i=1;i<=6;i++) assert.ok(!getElementById(`pstep-${i}`).classes.has('active'), 'idle shows no running step');
assert.equal(getElementById('sorting-now-step').textContent,'대기 중');
update({step:'initializing',step_num:1,is_running:true});
assert.match(getElementById('sorting-now-step').textContent,/STEP 1\/6 · 원점 복귀 진행 중/);
assert.equal(vm.runInContext('handleSortingUpdate.running',context),true,'stop/home button follows the server state');
// The SSE event carries the state under `sorting` (JC1060 starts must reach the PC dashboard).
assert.match(source,/handleSortingUpdate\(data\.sorting \|\| data\.status\)/);
const backend = fs.readFileSync(path.join(__dirname, '../backend/app.py'), 'utf8');
assert.match(backend,/"type": "sorting_update",\s*"sorting":/);
// A rejected start must not leave the buttons disabled forever. Reset is a real
// capture/sequence reset request, separate from the old home-only command.
const begin = source.indexOf('    const triggerStartSorting = async () => {');
const end = source.indexOf("    document.getElementById('sorting-start-btn')?.addEventListener", begin);
const urls = [];
context.fetch = async url => { urls.push(url); return { status: 409, ok: false, json: async () => ({ message: 'busy' }) }; };
context.addLog = () => {};
context.alert = () => {};
vm.runInContext(source.slice(begin, end) + '\nwindow.startTest=triggerStartSorting; window.resetTest=triggerResetSorting;', context);
(async () => {
    await context.window.startTest();
    assert.equal(getElementById('sorting-start-btn').disabled, false);
    context.fetch = async url => { urls.push(url); return { status: 200, ok: true, json: async () => ({ status: 'resetting' }) }; };
    vm.runInContext('handleSortingUpdate.running = false;', context);
    await context.window.resetTest();
    assert.equal(urls.at(-1), '/api/sorting/reset');
    console.log('Sorting dashboard regression checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
