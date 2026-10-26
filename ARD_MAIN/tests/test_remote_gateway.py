"""Exercise authentication, isolation from LAN APIs, and streaming proxying."""
import io
from pathlib import Path
import re
import sys
import unittest
from urllib3.response import HTTPResponse
from werkzeug.security import generate_password_hash

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from remote_gateway import create_app


class UpstreamResponse:
    def __init__(self, data=b"data: ready\n\n", headers=None):
        self.status_code = 200
        self.headers = headers or {"Content-Type": "text/event-stream"}
        self.raw = HTTPResponse(body=io.BytesIO(data), preload_content=False)
        self.closed = False

    def close(self):
        self.closed = True


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.password_hash = generate_password_hash("test-password")

    def setUp(self):
        self.calls = []
        self.responses = []
        self.now = 1000

        def transport(*args, **kwargs):
            self.calls.append((args, kwargs))
            response = UpstreamResponse()
            self.responses.append(response)
            return response

        self.app = create_app({"hostname": "ard.example.com", "password_hash": self.password_hash,
                               "session_secret": "test-secret-which-is-only-for-unit-tests"},
                              transport=transport, clock=lambda: self.now)
        self.app.testing = True
        self.client = self.app.test_client()
        self.base = "https://ard.example.com"

    def login(self, password="test-password"):
        page = self.client.get("/_ard/login", base_url=self.base)
        token = re.search(rb'name="csrf" value="([^"]+)"', page.data)[1].decode()
        return self.client.post("/_ard/login", base_url=self.base,
                                headers={"Origin": self.base}, data={"csrf": token, "password": password})

    def test_anonymous_requests_never_reach_main_program(self):
        for path in ("/api/config", "/api/terminal/execute", "/api/status/stream"):
            self.assertEqual(self.client.get(path, base_url=self.base).status_code, 401)
        self.assertEqual(self.client.get("/", base_url=self.base).status_code, 302)
        self.assertEqual(self.calls, [])

    def test_valid_login_sets_secure_cookie_and_streams_events(self):
        login = self.login()
        self.assertEqual(login.status_code, 303)
        cookie = login.headers["Set-Cookie"]
        for flag in ("Secure", "HttpOnly", "SameSite=Strict"):
            self.assertIn(flag, cookie)
        response = self.client.get("/api/status/stream", base_url=self.base)
        self.assertEqual(response.data, b"data: ready\n\n")
        self.assertEqual(response.headers["Cache-Control"], "no-store")
        self.assertTrue(self.responses[0].closed)
        self.assertNotIn("Cookie", self.calls[0][1]["headers"])

    def test_wrong_password_and_rate_limit(self):
        for _ in range(5):
            self.assertEqual(self.login("wrong-password").status_code, 401)
        self.assertEqual(self.login().status_code, 429)
        self.assertEqual(self.calls, [])
        self.now += 901
        self.assertEqual(self.login().status_code, 303)

    def test_csrf_and_cross_origin_writes_are_rejected(self):
        self.assertEqual(self.login().status_code, 303)
        for headers in ({}, {"Origin": "https://evil.example"}):
            response = self.client.post("/api/terminal/execute", base_url=self.base, headers=headers, json={"command": "version"})
            self.assertEqual(response.status_code, 403)
        self.assertEqual(self.calls, [])
        response = self.client.post("/_ard/login", base_url=self.base, headers={"Origin": self.base}, data={"csrf": "잘못된 토큰", "password": "test-password"})
        self.assertEqual(response.status_code, 403)

    def test_authenticated_post_keeps_body_and_query(self):
        self.login()
        response = self.client.post("/api/test?mode=one", base_url=self.base, headers={"Origin": self.base}, data=b"payload", content_type="application/octet-stream")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(self.calls[0][0], ("POST", "http://127.0.0.1:5000/api/test?mode=one"))
        self.assertEqual(self.calls[0][1]["data"], b"payload")

    def test_host_header_cannot_choose_upstream(self):
        self.assertEqual(self.client.get("/", base_url="https://evil.example").status_code, 400)
        with self.assertRaises(ValueError):
            create_app({"upstream": "https://evil.example", "hostname": "ard.example.com"})

    def test_tampered_session_is_rejected(self):
        self.client.set_cookie("__Host-ard_remote", "tampered", domain="ard.example.com")
        self.assertEqual(self.client.get("/api/config", base_url=self.base).status_code, 401)
        self.assertEqual(self.calls, [])

    def test_logout_ends_authenticated_session(self):
        self.login()
        page = self.client.get("/_ard/logout", base_url=self.base)
        token = re.search(rb'name="csrf" value="([^"]+)"', page.data)[1].decode()
        response = self.client.post("/_ard/logout", base_url=self.base, headers={"Origin": self.base}, data={"csrf": token})
        self.assertEqual(response.status_code, 303)
        self.assertEqual(self.client.get("/api/config", base_url=self.base).status_code, 401)


if __name__ == "__main__":
    unittest.main()
