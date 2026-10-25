const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/simulation-drive.js'), 'utf8');
const sendSource = source.slice(0, source.indexOf('// --- DriveMCU Dual-Axis'));

async function run(response) {
    const logs = [];
    const context = vm.createContext({
        fetch: async () => response,
        addLog: (kind, text) => logs.push(text),
    });
    vm.runInContext(sendSource + '\nthis.send = sendMotorCmd;', context);
    return { ok: await context.send('mxr200'), logs };
}

(async () => {
    let result = await run({ ok: true, json: async () => ({ status: 'success', delivery: 'accepted' }) });
    assert.equal(result.ok, true);
    assert.ok(result.logs[0].includes('accepted'));
    result = await run({ ok: false, status: 409, json: async () => ({ status: 'error', message: 'DriveMCU ERR:REJECTED' }) });
    assert.equal(result.ok, false);
    assert.ok(result.logs[0].includes('ERR:REJECTED'));
    result = await run({ ok: false, status: 504, json: async () => ({ status: 'error', message: 'ACK unconfirmed' }) });
    assert.equal(result.ok, false);
    assert.ok(result.logs[0].includes('ACK unconfirmed'));
    result = await run({ ok: true, status: 202, json: async () => ({ status: 'queued' }) });
    assert.equal(result.ok, true);
    assert.ok(result.logs[0].includes('awaiting acknowledgement'));
    assert.ok(!result.logs[0].includes('accepted'));
    console.log('PASS Drive delivery UI: accepted, rejected, unconfirmed, queued');
})().catch(error => { console.error(error); process.exitCode = 1; });
