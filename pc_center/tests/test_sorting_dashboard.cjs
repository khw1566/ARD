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
update({step:'rotating', step_num:3, is_running:true, target_yaw:270,
    dump_roll:45, target_rail_z:15, current_yaw:0, current_roll:0, current_rail_z:15});
assert.deepEqual(poses.at(-1),[0,0,15]);
assert.equal(getElementById('sorting-3d-yaw').textContent,'0.0°');
assert.equal(getElementById('sorting-reset-label').textContent,'선별 중단');
update({step:'error',is_running:false,step_desc:'MCU disconnected'});
assert.equal(poses.length,1);
assert.equal(getElementById('sorting-status-badge').textContent,'ERROR');
assert.equal(getElementById('sorting-start-btn').disabled,false);
update({step:'complete',step_num:5,is_running:false});
for(let i=1;i<=5;i++) assert.ok(getElementById(`pstep-${i}`).classes.has('completed'));
console.log('Sorting dashboard regression checks passed');
