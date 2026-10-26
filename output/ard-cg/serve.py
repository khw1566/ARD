"""Serve the local CG preview and only its explicitly mapped source assets."""
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import unquote, urlsplit
import mimetypes

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / '영상제작용 CAD파일'


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        request = unquote(urlsplit(self.path).path)
        if request == '/vendor/three.min.js':
            target = HERE / 'vendor/three.min.js'
        elif request.startswith('/cad/'):
            name = request[len('/cad/'):]
            target = SOURCE / name
            if Path(name).name != name:
                self.send_error(403)
                return
        else:
            target = (HERE / (request.lstrip('/') or 'index.html')).resolve()
            if not target.is_relative_to(HERE):
                self.send_error(403)
                return
        if not target.is_file():
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mimetypes.guess_type(target.name)[0] or 'application/octet-stream')
        self.send_header('Content-Length', str(target.stat().st_size))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        with target.open('rb') as stream:
            import shutil
            shutil.copyfileobj(stream, self.wfile)


if __name__ == '__main__':
    print('CG preview: http://127.0.0.1:5187',flush=True)
    ThreadingHTTPServer(('127.0.0.1', 5187), Handler).serve_forever()
