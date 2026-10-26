"""Loopback-only receiver for deterministic browser export. Never public."""
from pathlib import Path
from http.server import ThreadingHTTPServer
from datetime import datetime
import json
import os
import threading
from serve import Handler as PreviewHandler

HERE=Path(__file__).resolve().parent
PORT=int(os.environ.get('ARD_RENDER_PORT','5188'))
JOB=HERE/'render_jobs'/datetime.now().strftime('%Y%m%d-%H%M%S')
JOB.mkdir(parents=True,exist_ok=False)
LOCK=threading.Lock()
STATE={'status':'ready','job':str(JOB),'chunks':0,'bytes':0,'frames':0}

def save():
    (JOB/'status.json').write_text(json.dumps(STATE,ensure_ascii=False,indent=2),encoding='utf-8')
save()
(HERE/'render_jobs'/'latest.txt').write_text(str(JOB),encoding='utf-8')

class Handler(PreviewHandler):
    def log_message(self,*args):
        pass

    def reply(self,code,data):
        body=json.dumps(data).encode()
        self.send_response(code);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

    def do_GET(self):
        if self.path=='/render/status':
            self.reply(200,STATE)
        else:
            super().do_GET()

    def do_POST(self):
        if self.headers.get('Origin') != f'http://127.0.0.1:{PORT}':
            self.reply(403,{'error':'same-origin local exporter only'});return
        length=int(self.headers.get('Content-Length','0'))
        if length<0 or length>64*1024*1024:
            self.reply(413,{'error':'invalid payload size'});return
        body=self.rfile.read(length)
        with LOCK:
            if self.path=='/render/start':
                if STATE['status'] not in ['ready','error']:
                    self.reply(409,{'error':'export already started'});return
                config=json.loads(body)
                STATE.update(status='rendering',config=config,chunks=0,bytes=0,frames=0)
                (JOB/'video.h264').write_bytes(b'')
                save();self.reply(200,STATE)
            elif self.path=='/render/chunk':
                index=int(self.headers.get('X-Chunk-Index','-1'))
                if STATE['status']!='rendering' or index!=STATE['chunks']:
                    self.reply(409,{'error':'chunk sequence mismatch'});return
                with (JOB/'video.h264').open('ab') as stream:stream.write(body)
                STATE.update(chunks=index+1,bytes=STATE['bytes']+len(body),frames=int(self.headers.get('X-Frame-Count','0')))
                save();self.reply(200,{'ok':True})
            elif self.path=='/render/complete':
                STATE.update(status='complete',result=json.loads(body));save();self.reply(200,STATE)
            elif self.path=='/render/error':
                STATE.update(status='error',error=json.loads(body));save();self.reply(200,STATE)
            else:self.reply(404,{'error':'unknown route'})

if __name__=='__main__':
    print(f'RENDER_SERVER http://127.0.0.1:{PORT}/export.html\nJOB {JOB}',flush=True)
    ThreadingHTTPServer(('127.0.0.1',PORT),Handler).serve_forever()
