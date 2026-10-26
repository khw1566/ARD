/* Check sequential actuator motion and the single shared display transcript. */
const assert=require('node:assert/strict'),fs=require('node:fs'),shots=require('./cinematic_plan.js');
const at=u=>shots.frameAt(115+14*u),near=(a,b)=>Math.abs(a-b)<1e-8;
const start=at(0),moved=at(.21),yawed=at(.41),tilted=at(.61),neutral=at(.81),end=at(1-1e-8);
assert(near(start.rail,0)&&near(moved.rail,30));
assert(near(moved.a,0)&&near(moved.b,0));
assert(near(yawed.rail,30)&&near(yawed.a,40)&&near(yawed.b,40));
assert(near(tilted.rail,30)&&near(tilted.a,65)&&near(tilted.b,15));
assert(near(neutral.rail,30)&&near(neutral.a,0)&&near(neutral.b,0));
assert(end.rail<1e-8&&near(end.a,0)&&near(end.b,0));
const stages=new Set();let previous=at(0);
for(let i=1;i<840;i++){
 const f=at(i/840);stages.add(f.step);
 const dr=Math.abs(f.rail-previous.rail),da=Math.abs(f.a-previous.a)+Math.abs(f.b-previous.b);
 assert(!(dr>1e-8&&da>1e-8),'Rail and plate must not change together');
 assert.deepEqual(f.camera,previous.camera,'Sequence camera must hold');previous=f;
}
assert.equal(stages.size,5);
const captions=JSON.parse(fs.readFileSync(__dirname+'/narration/cg_captions.json','utf8'));
for(const list of [captions.cg,captions.intro])for(let i=0;i<list.length;i++){
 assert(list[i].end>list[i].start);if(i)assert(list[i].start>=list[i-1].end-1e-9);
}
const html=fs.readFileSync(__dirname+'/cinematic.html','utf8');
for(const removed of ['id="leaders"','id="planet-leaders"','id="chapter"','class="progress"','id="sound"'])assert(!html.includes(removed),removed);
console.log(JSON.stringify({passed:true,sequentialMotionSamples:840,stages:5,cgCaptionCues:captions.cg.length,introCaptionCues:captions.intro.length,sourceOnlyChecks:true}));
