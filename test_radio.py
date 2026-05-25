import subprocess
urls = [
    {'name': 'TBS', 'url': 'http://tbs.hscdn.com/tbsradio/fm/playlist.m3u8'},
    {'name': 'Arirang', 'url': 'https://worldbeat.arirang.com/'},
    {'name': 'Lofi Zeno', 'url': 'http://stream.zeno.fm/f3wvbbqmdg8uv'}
]
for u in urls:
    p = subprocess.run(['ffmpeg', '-y', '-headers', 'User-Agent: Mozilla/5.0\r\n', '-rw_timeout', '5000000', '-i', u['url'], '-t', '1', '-f', 'null', '-'], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    print(u['name'], 'OK' if p.returncode == 0 else 'FAIL')
