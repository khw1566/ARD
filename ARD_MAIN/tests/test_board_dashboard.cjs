const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../frontend/static/js/board-settings.js'), 'utf8');
const hidden = new Set();
const element = (extra = {}) => ({style:{}, dataset:{}, textContent:'', classList:{toggle(name, on) { if(on) hidden.add(this); else hidden.delete(this); }}, ...extra});
const robot = element(), legacy = element(), jcOnly = element(), voiceNote = element();
const hmiLabel = element({dataset:{boardLabel:'hmi'}});
const driveLabel = element({dataset:{boardLabel:'drive'}});
const elements = new Map([['jc1060-ip', element()], ['jc1060-connection', element()]]);
const groups = {'[data-board="robot3axismcu"]':[robot], '[data-legacy-only]':[legacy], '[data-jc-only]':[jcOnly], '[data-jc-voice-note]':[voiceNote], '[data-board-label]':[hmiLabel,driveLabel]};
const tabs = [];
const context = vm.createContext({window:{ARD_HARDWARE:{robot3axis_enabled:true, control_terminal:'legacy', voice_device:'controlmcu', jc1060_ip:''}},
    document:{getElementById:id => elements.get(id), querySelectorAll:selector => groups[selector] || [], addEventListener(){}},
    localStorage:{getItem:()=>'ko'}});
vm.runInContext(source, context);
const ui = context.window.BoardUI;
ui.apply();
assert.equal(hmiLabel.textContent, 'HMI UI 보드');
assert.equal(driveLabel.textContent,'모터 구동 보드');
assert.equal(ui.isTabEnabled('robot3axismcu-tab'), true);
context.window.activeDashboardTab = 'robot3axismcu-tab';
context.window.switchTab = tab => tabs.push(tab);
ui.apply({robot3axis_enabled:false, control_terminal:'jc1060', voice_device:'controlmcu', jc1060_ip:''});
assert.equal(tabs.at(-1), 'sorting-tab', 'turning off robot exits the active robot view');
assert.equal(ui.isTabEnabled('robot3axismcu-tab'), false);
assert.equal(hmiLabel.textContent, 'JC1060P470C');
assert.equal(driveLabel.textContent,'모터 구동 보드');
assert.ok(hidden.has(robot.classList));
assert.ok(hidden.has(legacy.classList));
assert.ok(!hidden.has(jcOnly.classList));
ui.updateStatus({hardware:ui.getSettings(), jc1060:{ip:'10.0.0.42', state:'Online'}});
assert.equal(elements.get('jc1060-ip').textContent, '10.0.0.42');
assert.equal(elements.get('jc1060-connection').textContent, '온라인');
ui.apply({robot3axis_enabled:true, control_terminal:'legacy', voice_device:'jc1060', jc1060_ip:''});
assert.ok(!hidden.has(robot.classList), 're-enable restores the board');
assert.ok(!hidden.has(legacy.classList), 'switching back restores legacy UI');
assert.ok(!hidden.has(voiceNote.classList), 'JC voice requirement stays visible');
assert.equal(ui.isJC(), false);

const robotSource = fs.readFileSync(path.join(__dirname, '../frontend/static/js/robot3axis-controller.js'), 'utf8');
const queueStart = robotSource.indexOf('    let r3CommandQueue = [];');
const queueEnd = robotSource.indexOf('    function updateR3QueueUI()', queueStart);
const cancelStart = robotSource.indexOf('    window.onRobotBoardDisabled = function()');
const queueContext = vm.createContext({window:{BoardUI:{isTabEnabled:()=>false}},
    document:{getElementById:()=>null}, clearTimeout(){}, clearInterval(){},
    updateR3QueueUI(){}, r3StatusTimer:null, r3StatusInterval:null, isSequenceRunning:false,
    fetch(){throw new Error('Disabled robot must not send requests');}});
vm.runInContext(robotSource.slice(queueStart,queueEnd) + robotSource.slice(cancelStart), queueContext);
vm.runInContext("addR3Command('disabled',1,{command:'rx100'})",queueContext);
assert.equal(vm.runInContext('r3CommandQueue.length',queueContext),0);
vm.runInContext("r3CommandQueue.push({name:'stale'}); r3IsExecuting=true; window.onRobotBoardDisabled()",queueContext);
assert.equal(vm.runInContext('r3CommandQueue.length',queueContext),0);
assert.equal(vm.runInContext('r3IsExecuting',queueContext),false);
assert.equal(vm.runInContext('r3QueueGeneration',queueContext),1);
console.log('Board dashboard switching checks passed');
