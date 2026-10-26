const {chromium} = require(process.env.ARD_PLAYWRIGHT_MODULE || 'playwright');
const path = require('node:path');
(async()=>{
 const browser=await chromium.launch({channel:'chrome',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const page=await browser.newPage({viewport:{width:1600,height:980},deviceScaleFactor:1});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:5187/',{waitUntil:'domcontentloaded'});
 await page.waitForFunction(()=>window.cgReady,{timeout:60000});
 for(const [name,time] of [['assembly',4],['exploded',20],['bevel',29]]){
   await page.evaluate(t=>window.seekCG(t),time);
   await page.locator('#frame').screenshot({path:path.join(__dirname,`preview_${name}.png`)});
 }
 console.log(JSON.stringify({ready:await page.evaluate(()=>window.cgReady),errors}));
 if(errors.length)process.exitCode=1;
 await browser.close();
})().catch(e=>{console.error(e);process.exitCode=1;});
