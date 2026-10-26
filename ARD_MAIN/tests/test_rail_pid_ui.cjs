// Run the real UI handlers with mocked HTTP/DOM. No server, board or build.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const elements = new Map(), events = new Map(), calls = [];
function element(id) {
    if (!elements.has(id)) elements.set(id, {
        _value: '', get value() { return this._value; }, set value(v) { this._value = String(v); },
        textContent: '', offsetParent: {}, disabled: false,
        classList: { toggle() {} }, querySelectorAll: () => [],
        handlers: {}, addEventListener(type, fn) { this.handlers[type] = fn; },
        checkValidity() { return this.value !== '' && Number.isFinite(Number(this.value)) && Number(this.value) >= 0; },
    });
    return elements.get(id);
}
const config = { rail_dir: -1, steps_per_mm: 23.683, mode: 'pid', settle_s: .3, timeout_s: 15,
    center_mm: 95, plus_mm: 70, minus_mm: 120, capture_mm: 120,
    taught: { center: true, plus: true, minus: true }, pid_p: .12, pid_i: 0, pid_d: 0, tilt_deg: 45,
    destinations: { Metal: { rail: 'plus', tilt: 'forward' }, Glass: { rail: 'minus', tilt: 'forward' },
        Others: { rail: 'minus', tilt: 'forward' }, Paper: { rail: 'plus', tilt: 'backward' },
        Plastic: { rail: 'minus', tilt: 'backward' } } };
const live = { live: true, pid: false, busy: false, jogging: false, tof_ok: true, tof_mm: 120,
    rail_target: 120, rail_dir: -1, steps_per_mm: 23.683, pid_p: .3, pid_i: .01, pid_d: 5 };
let reject = false;
const context = vm.createContext({
    document: { getElementById: element, querySelectorAll: () => [], activeElement: null },
    window: { addEventListener: (name, fn) => events.set(name, fn), dispatchEvent() {} },
    CustomEvent: class { constructor(type, options) { this.type = type; Object.assign(this, options); } },
    setInterval() {}, alert() {}, confirm: () => false,
    fetch: async (url, opts) => {
        calls.push([url, opts]);
        if (url === '/api/rail/pid') {
            if (reject) return { ok: false, status: 409, json: async () => ({ status: 'error', message: 'rejected' }) };
            const gains = JSON.parse(opts.body);
            Object.assign(config, gains); Object.assign(live, gains);
        }
        return { ok: true, status: 200, json: async () => ({ status: 'success', config, result: live }) };
    },
});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../frontend/static/js/rail-tools.js'), 'utf8'), context);
const flush = () => new Promise(resolve => setImmediate(resolve));
(async () => {
    await flush();
    events.get('ard-drive-live')({ detail: live });
    assert.match(element('pid-gains-live').textContent, /P 0.3 \/ I 0.01 \/ D 5/);
    assert.equal(element('rail-home-mm').value, '120', 'origin shown from home_mm (older capture_mm accepted)');
    const before = calls.length;
    element('pid-p-input').value = '0.15';
    element('pid-p-input').handlers.input();
    events.get('ard-drive-live')({ detail: live });
    assert.equal(element('pid-p-input').value, '0.15', 'live updates preserve edits');
    assert.equal(calls.length, before, 'editing alone never sends motor commands');
    await element('pid-gains-apply').handlers.click();
    assert.equal(calls.filter(([url]) => url === '/api/rail/pid').length, 1);
    assert.equal(config.pid_p, .15);
    assert.match(element('pid-gains-feedback').textContent, /완료/);
    live.pid = true;
    events.get('ard-drive-live')({ detail: live });
    assert.equal(element('pid-gains-apply').disabled, true);
    live.pid = false;
    events.get('ard-drive-live')({ detail: live });
    reject = true;
    element('pid-p-input').value = '.2'; element('pid-p-input').handlers.input();
    await element('pid-gains-apply').handlers.click();
    assert.match(element('pid-gains-feedback').textContent, /적용 실패/);
    assert.equal(element('pid-p-input').value, '.2', 'failed apply preserves the draft');
    assert.equal(config.pid_p, .15, 'failed apply does not save');
    // Bins are told apart by color: each card and table row keeps its material color.
    const guide = element('bin-guide').innerHTML;
    for (const [name, color, border] of [['금속/캔', '#fbbf24', 'rgba(245, 158, 11, 0.3)'], ['종이/팩', '#4ade80', 'rgba(16, 185, 129, 0.3)'],
        ['플라스틱', '#60a5fa', 'rgba(59, 130, 246, 0.3)'], ['유리', '#cbd5e1', 'rgba(148, 163, 184, 0.3)'], ['기타', '#cbd5e1', 'rgba(148, 163, 184, 0.3)']]) {
        assert.ok(guide.includes(`<span style="color: ${color};">${name}</span>`), `${name} card text color`);
        assert.ok(guide.includes(`border: 1px solid ${border}`), `${name} card border`);
    }
    // The color follows the material, not the rail side or tilt.
    config.destinations.Metal = { rail: 'minus', tilt: 'backward' };
    await element('rail-dest-save-btn').handlers.click();
    assert.ok(element('rail-dest-body').innerHTML.includes('color: #fbbf24; font-weight: 600;">금속/캔'));
    console.log('Rail PID UI: current values, edits, apply, active guard, rejection and bin colors passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
