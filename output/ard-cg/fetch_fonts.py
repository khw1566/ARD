"""Download project-local IBM Plex fonts and retain source/license records."""
from pathlib import Path
import urllib.request,json,hashlib
dest=Path(__file__).resolve().parent/'fonts';dest.mkdir(exist_ok=True)
files={name:'https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexsanskr/'+name for name in ['IBMPlexSansKR-Regular.ttf','IBMPlexSansKR-Medium.ttf','OFL.txt']}
files['IBMPlexMono-Regular.ttf']='https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf'
records=[]
for name,url in files.items():
    data=urllib.request.urlopen(url,timeout=30).read();(dest/name).write_bytes(data)
    records.append({'file':name,'url':url,'sha256':hashlib.sha256(data).hexdigest()});print(name,len(data),flush=True)
(dest/'sources.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
