"""Render an isolated drive-model pose in headless Chrome, with no MCU APIs."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--three', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'frontend/static/js/simulation-drive.js')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--yaw', type=float, default=0)
    parser.add_argument('--roll', type=float, default=0)
    parser.add_argument('--rail', type=float, default=0)
    parser.add_argument('--view', choices=['perspective', 'front', 'gears'], default='perspective')
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='drive-render-', dir=output.parent) as folder:
        folder = Path(folder)
        camera_pose = {'perspective': 'camera.position.set(38,34,42); camera.lookAt(0,0,0);',
                       'front': 'camera.position.set(42,18,0); camera.lookAt(0,1,0);',
                       'gears': 'camera.position.set(15,11,18); camera.lookAt(0,7.5,0); camera.fov=36;'}[args.view]
        page = folder / 'pose.html'
        page.write_text(f'''<!doctype html><meta charset="utf-8">
<style>html,body{{margin:0;background:#1b2738;color:#e2e8f0;font:16px sans-serif;overflow:hidden}}
#sorting-three-container{{width:1100px;height:850px}}#caption{{position:absolute;top:18px;left:22px}}#result{{display:none}}</style>
<div id="sorting-three-container"></div><div id="caption">Yaw {args.yaw:g}° · Tilt {args.roll:g}° · Rail {args.rail:g} mm</div><pre id="result"></pre>
<button id="sync-d-ip" hidden></button>
<script>window.requestAnimationFrame=()=>1;window.fetch=()=>{{throw Error('Hardware API disabled in render fixture')}};
window.Chart=function(){{}};window.addLog=()=>{{}};</script>
<script src="{args.three.resolve().as_uri()}"></script>
<script src="{args.source.resolve().as_uri()}"></script>
<script>
try {{
    initThreeJS(); clock={{getDelta:()=>1}};
    angleChart={{data:{{datasets:[{{data:[]}},{{data:[]}}]}},update(){{}}}};
    window.setSortingAngles({args.yaw},{args.roll},{args.rail});
    {camera_pose} camera.updateProjectionMatrix(); animate(); scene.updateMatrixWorld(true);
    document.body.dataset.renderState='ok';
    document.getElementById('result').textContent=JSON.stringify({{state:predictedState,revision:THREE.REVISION}});
}} catch(error) {{document.body.dataset.renderState='error';document.getElementById('result').textContent=error.stack;}}
</script>''', encoding='utf-8')
        command = [r'C:\Program Files\Google\Chrome\Application\chrome.exe', '--headless=new',
                   '--no-first-run', '--no-default-browser-check', '--disable-extensions',
                   '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
                   '--allow-file-access-from-files', '--hide-scrollbars', '--window-size=1100,850',
                   '--virtual-time-budget=500', '--user-data-dir=' + str(folder / 'profile'),
                   '--screenshot=' + str(output), '--dump-dom', page.as_uri()]
        result = subprocess.run(command, capture_output=True, timeout=45, creationflags=0x08000000)
        dom = result.stdout.decode('utf-8', errors='replace')
        match = re.search(r'<pre id="result">(.*?)</pre>', dom, flags=re.S)
        if result.returncode or 'data-render-state="ok"' not in dom or not output.exists():
            raise RuntimeError((match.group(1) if match else dom[-1000:]) + '\n' + result.stderr.decode(errors='replace')[-1200:])
        print(json.dumps({'image': str(output), 'result': match.group(1) if match else ''}, ensure_ascii=False))


if __name__ == '__main__':
    main()
