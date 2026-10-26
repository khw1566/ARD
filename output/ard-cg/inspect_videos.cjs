const {chromium} = require(process.env.ARD_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
(async () => {
  const browser = await chromium.launch({channel:'chrome',headless:true});
  const page = await browser.newPage({viewport:{width:1440,height:900}});
  await page.goto('http://127.0.0.1:5187/');
  const files = fs.readdirSync(path.resolve('영상제작용 CAD파일')).filter(x=>x.endsWith('.mp4'));
  const report = [];
  for (let i=0;i<files.length;i++) {
    const info = await page.evaluate(async (file) => {
      const video = document.createElement('video');
      video.src = '/cad/'+encodeURIComponent(file); video.muted=true; video.preload='auto';
      await new Promise((ok,fail)=>{ video.onloadeddata=ok; video.onerror=()=>fail(new Error('Video load error')); });
      return {duration:video.duration,width:video.videoWidth,height:video.videoHeight};
    },files[i]);
    report.push({file:files[i],...info});
    const frames = await page.evaluate(async ({file,duration}) => {
      const video=document.createElement('video');video.muted=true;video.playsInline=true;
      video.style.cssText='position:fixed;width:320px;top:0;left:0;z-index:100';document.body.appendChild(video);
      video.src='/cad/'+encodeURIComponent(file);
      await new Promise((ok,fail)=>{video.onloadeddata=ok;video.onerror=fail;});
      const canvas=document.createElement('canvas');canvas.width=video.videoWidth;canvas.height=video.videoHeight;
      const ctx=canvas.getContext('2d'),out=[],targets=[.1,.3,.55,.8].map(x=>duration*x);
      await video.play();
      await new Promise((resolve,reject)=>{
        const deadline=setTimeout(()=>reject(new Error('Video frame timeout')),30000);
        function next(now,meta){
          if(meta.mediaTime>=targets[out.length]){ctx.drawImage(video,0,0);out.push(canvas.toDataURL('image/png').split(',')[1]);}
          if(out.length===4){clearTimeout(deadline);video.pause();resolve();}else video.requestVideoFrameCallback(next);
        }
        video.requestVideoFrameCallback(next);
      });video.remove();return out;
    },{file:files[i],duration:Math.min(info.duration,12)});
    frames.forEach((png,j)=>fs.writeFileSync(path.join(__dirname,`video_${i+1}_${j+1}.png`),Buffer.from(png,'base64')));
  }
  fs.writeFileSync(path.join(__dirname,'video_inventory.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify(report));
  await browser.close();
})().catch(e=>{console.error(e);process.exitCode=1;});
