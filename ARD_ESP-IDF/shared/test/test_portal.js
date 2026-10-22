const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../portal_page.h'), 'utf8');
const html = [...source.matchAll(/^"((?:\\.|[^"\\])*)";?$/gm)]
  .map(m => JSON.parse('"' + m[1] + '"')).join('');
const script = html.match(/<script>([\s\S]*)<\/script>/)[1];

async function run() {
  const elements = Object.fromEntries(['networks', 'scan-status', 'rescan', 'ssid', 'password'].map(id => [id, {
    value: '', textContent: '', disabled: false, options: [],
    replaceChildren(...items) { this.options = items; }, add(item) { this.options.push(item); }
  }]));
  elements.ssid.value = 'configured'; elements.password.value = 'configured-password';
  const queries = [], timers = [];
  let data = { busy: true };
  const context = vm.createContext({
    document: { getElementById: id => elements[id] },
    Option: function(text, value) { this.textContent = text; this.value = value; },
    fetch: async url => { queries.push(url); return { ok: true, json: async () => data }; },
    setTimeout: fn => timers.push(fn)
  });
  vm.runInContext(script, context);
  await new Promise(setImmediate);
  assert.equal(queries[0], '/network/scan?refresh=1');
  assert.equal(timers.length, 1); assert.equal(elements.rescan.disabled, true);
  const hostileSSID = '<img src=x onerror=alert(1)>&"';
  data = { busy: false, error: 0, networks: [
    { ssid: hostileSSID, rssi: -55, open: false, saved: true, password: 'saved-password' },
    { ssid: 'open', rssi: -70, open: true, saved: false, password: '' }
  ] };
  timers.shift()(); await new Promise(setImmediate);
  assert.equal(queries[1], '/network/scan'); assert.equal(elements.networks.options.length, 3);
  assert.equal(elements.networks.options[1].value, hostileSSID);
  elements.networks.value = hostileSSID; elements.networks.onchange();
  assert.equal(elements.ssid.value, hostileSSID); assert.equal(elements.password.value, 'saved-password');
  elements.networks.value = 'configured'; elements.networks.onchange();
  assert.equal(elements.password.value, 'configured-password');
  elements.networks.value = 'open'; elements.networks.onchange();
  assert.equal(elements.password.value, ''); assert.equal(elements.rescan.disabled, false);
  data = { busy: false, error: -1, networks: [] };
  elements.rescan.onclick(); await new Promise(setImmediate);
  assert.match(elements['scan-status'].textContent, /검색 실패/); assert.equal(elements.rescan.disabled, false);
  assert(html.includes("type='text' name='password'"));
  assert(!script.includes('innerHTML'));
  console.log('PASS portal: automatic scan, polling, safe SSID text, saved password selection, open AP, retry');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
