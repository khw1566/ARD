"""Password protected HTTP gateway for the local ARD server.

Only this loopback listener is published by Cloudflare Tunnel. MCU traffic can
continue using the original server without browser authentication.
"""
import argparse
from collections import defaultdict, deque
from datetime import timedelta
import hmac
import json
from pathlib import Path
import secrets
import threading
import time
from urllib.parse import urlsplit

from flask import Flask, Response, abort, redirect, render_template_string, request, session, stream_with_context
import requests
from werkzeug.security import check_password_hash

LOGIN_PAGE = """<!doctype html>
<html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>ARD 로그인</title><style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;min-height:100dvh;display:grid;place-items:center;background:#101722;color:#edf3fa;font:16px system-ui;padding:24px max(16px,env(safe-area-inset-right)) max(24px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
main{width:min(400px,100%);min-width:0;padding:clamp(24px,6vw,36px);background:#192331;border:1px solid #34475c;border-radius:16px}
h1{margin:0 0 12px;font-size:28px}p{color:#b2c2d2;line-height:1.6}label{display:block;margin:24px 0 8px}
input,button{width:100%;font:inherit;padding:13px;border-radius:8px}input{border:1px solid #59718b;background:#101722;color:white}
button{margin-top:16px;min-height:48px;background:#62c9ff;color:#092334;border:0;font-weight:700;cursor:pointer;touch-action:manipulation}.error{color:#ffb2ab}
input:focus-visible,button:focus-visible{outline:3px solid #62c9ff;outline-offset:3px}
@media(max-height:500px){body{place-items:start center}h1{font-size:24px}label{margin-top:12px}}
</style><main><h1>ARD Control Center</h1><p>메인 프로그램에 접속하려면 비밀번호를 입력하세요.</p>
{% if error %}<p class="error" role="alert">{{ error }}</p>{% endif %}
<form method="post"><input type="hidden" name="csrf" value="{{ csrf }}">
<label for="password">접속 비밀번호</label><input id="password" name="password" type="password" autocomplete="current-password" required autofocus>
<button type="submit">접속하기</button></form></main></html>"""

HOP_HEADERS = {"connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
               "te", "trailer", "transfer-encoding", "upgrade"}


def create_app(config, transport=None, clock=time.monotonic):
    app = Flask(__name__, static_folder=None)
    upstream = config.get("upstream", "http://127.0.0.1:5000").rstrip("/")
    parsed = urlsplit(upstream)
    if parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost") or parsed.path:
        raise ValueError("Upstream must be a loopback HTTP server")
    public_origin = "https://" + config["hostname"]
    app.config.update(
        SECRET_KEY=config["session_secret"], SESSION_COOKIE_NAME="__Host-ard_remote",
        SESSION_COOKIE_SECURE=True, SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict", PERMANENT_SESSION_LIFETIME=timedelta(hours=8),
        SESSION_REFRESH_EACH_REQUEST=False, MAX_CONTENT_LENGTH=32 * 1024 * 1024,
        TRUSTED_HOSTS=[config["hostname"], "localhost", "127.0.0.1"],
    )
    failures = defaultdict(deque)
    failure_lock = threading.Lock()
    send = transport or requests.request

    def csrf_valid():
        expected = str(session.get("csrf", "")).encode("utf-8")
        provided = str(request.form.get("csrf", "")).encode("utf-8")
        return bool(expected) and hmac.compare_digest(expected, provided)

    @app.before_request
    def require_session():
        if request.host.split(":", 1)[0] not in (config["hostname"], "localhost", "127.0.0.1"):
            abort(400)
        if request.method not in ("GET", "HEAD", "OPTIONS"):
            # Browser same-origin POSTs carry Origin; rejecting missing Origin
            # also prevents non-browser callers from silently bypassing this gate.
            if request.headers.get("Origin") != public_origin:
                abort(403)
        if request.path in ("/_ard/login", "/_ard/logout"):
            return None
        if not session.get("authenticated"):
            if request.path.startswith("/api/"):
                return {"error": "login_required"}, 401
            return redirect("/_ard/login", code=302)

    @app.after_request
    def private_response(response):
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "same-origin"
        response.headers["Strict-Transport-Security"] = "max-age=31536000"
        for name in list(response.headers.keys()):
            if name.lower().startswith("access-control-"):
                del response.headers[name]
        return response

    @app.route("/_ard/login", methods=["GET", "POST"])
    def login():
        session.setdefault("csrf", secrets.token_urlsafe(24))
        error = None
        if request.method == "POST":
            if not csrf_valid():
                abort(403)
            # This listener is loopback-only, reached by cloudflared. Its edge
            # overwrites CF-Connecting-IP, unlike caller supplied X-Forwarded-For.
            client = request.headers.get("CF-Connecting-IP", request.remote_addr)
            now = clock()
            with failure_lock:
                # Keep memory bounded, including scans from many source IPs.
                for key in list(failures):
                    while failures[key] and now - failures[key][0] > 900:
                        failures[key].popleft()
                    if not failures[key]:
                        del failures[key]
                if len(failures.get(client, ())) >= 5 or sum(map(len, failures.values())) >= 100:
                    return render_template_string(LOGIN_PAGE, csrf=session["csrf"], error="잠시 후 다시 시도하세요."), 429, {"Retry-After": "900"}
                failures[client].append(now)
            if check_password_hash(config["password_hash"], request.form.get("password", "")):
                with failure_lock:
                    failures.pop(client, None)
                session.clear()
                session["authenticated"] = True
                session["csrf"] = secrets.token_urlsafe(24)
                session.permanent = True
                return redirect("/", code=303)
            error = "비밀번호가 올바르지 않습니다."
        return render_template_string(LOGIN_PAGE, csrf=session["csrf"], error=error), 401 if error else 200

    @app.route("/_ard/logout", methods=["GET", "POST"])
    def logout():
        if request.method == "GET":
            if not session.get("authenticated"):
                return redirect("/_ard/login", code=302)
            return render_template_string("""<!doctype html><html lang="ko"><meta charset="utf-8">
                <meta name="viewport" content="width=device-width,initial-scale=1"><title>ARD 로그아웃</title>
                <h1>외부 접속 로그아웃</h1><form method="post">
                <input type="hidden" name="csrf" value="{{ csrf }}"><button>로그아웃</button></form>
                <p><a href="/">메인 프로그램으로 돌아가기</a></p></html>""", csrf=session["csrf"])
        if not csrf_valid():
            abort(403)
        session.clear()
        return redirect("/_ard/login", code=303)

    @app.route("/", defaults={"path": ""}, methods=["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"])
    @app.route("/<path:path>", methods=["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"])
    def proxy(path):
        # Concatenate to a fixed authority; never use urljoin with user input.
        url = upstream + "/" + path
        if request.query_string:
            url += "?" + request.query_string.decode("ascii")
        connection_tokens = {h.strip().lower() for h in request.headers.get("Connection", "").split(",")}
        excluded = HOP_HEADERS | connection_tokens | {"host", "cookie", "authorization", "content-length", "accept-encoding"}
        headers = {k: v for k, v in request.headers if k.lower() not in excluded}
        headers["Accept-Encoding"] = "identity"
        headers["X-ARD-Remote-Gateway"] = "1"
        try:
            origin = send(request.method, url, headers=headers, data=request.get_data(), stream=True,
                          allow_redirects=False, timeout=(5, 300))
        except requests.RequestException:
            return "메인 프로그램이 응답하지 않습니다. PC에서 실행 상태를 확인하세요.", 502
        excluded_response = HOP_HEADERS | {"content-length", "set-cookie", "content-encoding"}
        response_headers = [(k, v) for k, v in origin.headers.items() if k.lower() not in excluded_response]
        if "Location" in origin.headers and origin.headers["Location"].startswith(upstream + "/"):
            response_headers = [(k, v) for k, v in response_headers if k.lower() != "location"]
            response_headers.append(("Location", origin.headers["Location"][len(upstream):]))

        @stream_with_context
        def body():
            try:
                # read1 yields available bytes immediately for EventSource,
                # rather than waiting for an entire chunk to accumulate.
                while True:
                    chunk = origin.raw.read1(65536, decode_content=True)
                    if not chunk:
                        break
                    yield chunk
            finally:
                origin.close()
        response = Response(body(), status=origin.status_code, headers=response_headers)
        response.call_on_close(origin.close)
        return response

    return app


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=Path(__file__).parent / ".remote-access/gateway.json")
    args = parser.parse_args()
    settings = json.loads(args.config.read_text(encoding="utf-8"))
    from waitress import serve
    serve(create_app(settings), host="127.0.0.1", port=settings.get("port", 5006),
          threads=32, channel_timeout=360, max_request_body_size=32 * 1024 * 1024)
