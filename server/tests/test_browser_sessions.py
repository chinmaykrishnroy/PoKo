import base64
import concurrent.futures
import hashlib
import os
from pathlib import Path
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import replace
from unittest.mock import patch

from helpers import workspace_tempdir
from server.poko_server.config import LibraryConfig, SecurityConfig, load_config, config_to_yaml
from server.poko_server.http_api import make_server
from server.poko_server.security import Authentication, password_hash


class SessionTests(unittest.TestCase):
    def test_password_change_persists_revokes_sessions_and_operator_reset_wins(self):
        with workspace_tempdir() as root:
            path = root / "admin-verifier.json"
            initial = password_hash("correct horse test only")
            values = {"POKO_ADMIN_PASSWORD_HASH": initial, "POKO_ADMIN_PASSWORD_HASH_FILE": str(path)}
            with patch.dict(os.environ, values):
                auth = Authentication(True, "https://poko.example.com")
                token, _ = auth.login("admin", "correct horse test only", True)
                auth.change_password("correct horse test only", "new memorable password phrase", "new memorable password phrase")
                self.assertIsNone(auth.cookie_role("poko_session=" + token))
                self.assertNotIn("new memorable password phrase", path.read_text())
                restarted = Authentication(True, "https://poko.example.com")
                self.assertIsNotNone(restarted.login("admin", "new memorable password phrase", False)[0])
                self.assertIsNone(restarted.login("admin", "correct horse test only", False)[0])
            with patch.dict(os.environ, {**values, "POKO_ADMIN_PASSWORD_HASH": password_hash("operator recovery password")}):
                recovered = Authentication(True, "https://poko.example.com")
                self.assertIsNotNone(recovered.login("admin", "operator recovery password", False)[0])
                self.assertIsNone(recovered.login("admin", "new memorable password phrase", False)[0])

    def test_tab_returns_logout_expiry_and_remember(self):
        with patch.dict(os.environ, {"POKO_ADMIN_PASSWORD_HASH": password_hash("correct horse test only")}):
            auth = Authentication(True, "https://poko.example.com")
        token, limited = auth.login("admin", "correct horse test only", False)
        self.assertFalse(limited)
        cookie = "poko_session=" + token
        for _ in range(100):
            self.assertEqual(auth.cookie_role(cookie), "admin")
        with patch("server.poko_server.security.time.time", return_value=time.time() + 13 * 3600):
            self.assertIsNone(auth.cookie_role(cookie))
        remembered, _ = auth.login("admin", "correct horse test only", True)
        with patch("server.poko_server.security.time.time", return_value=time.time() + 29 * 86400):
            self.assertEqual(auth.cookie_role("poko_session=" + remembered), "admin")
        auth.logout(cookie)
        self.assertIsNone(auth.cookie_role(cookie))
        self.assertIsNone(auth.cookie_role("poko_session=forged"))

    def test_login_attempt_limit(self):
        with patch.dict(os.environ, {"POKO_ADMIN_PASSWORD_HASH": password_hash("correct horse test only")}):
            auth = Authentication(True, "https://poko.example.com")
        for _ in range(10):
            self.assertEqual(auth.login("admin", "incorrect", False), (None, False))
        self.assertEqual(auth.login("admin", "incorrect", False), (None, True))

    def test_http_login_parallel_tabs_and_logout_without_native_prompts(self):
        with workspace_tempdir() as root, patch.dict(os.environ, {
            "POKO_ADMIN_PASSWORD_HASH": password_hash("correct horse test only"),
            "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(b"test-device-token").hexdigest(),
        }):
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0,
                             security=SecurityConfig(True, "https://poko.example.com"),
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "index.db"))
            config_path = root / "config.yml"
            config_path.write_text(config_to_yaml(config), encoding="utf-8")
            server = make_server(config, dry_run=True, config_path=config_path)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            base = f"http://127.0.0.1:{server.server_address[1]}"
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self, *args):
                    return None
            opener = urllib.request.build_opener(NoRedirect)
            def request(path, data=None, extra=None):
                headers = {"Sec-Fetch-Mode": "cors", **(extra or {})}
                req = urllib.request.Request(base + path, data=data, headers=headers)
                try:
                    response = opener.open(req, timeout=10)
                except urllib.error.HTTPError as exc:
                    response = exc
                with response:
                    return response.status, response.headers, response.read()
            try:
                status, headers, _ = request("/")
                self.assertEqual(status, 303)
                self.assertEqual(headers["Location"], "/login")
                self.assertNotIn("WWW-Authenticate", headers)
                form = urllib.parse.urlencode({"username": "admin", "password": "correct horse test only", "remember": "1"}).encode()
                status, headers, _ = request("/api/security/login", form, {"Origin": "https://poko.example.com"})
                self.assertEqual(status, 303)
                cookie = headers["Set-Cookie"]
                for attribute in ("Secure", "HttpOnly", "SameSite=Strict", "Max-Age=2592000"):
                    self.assertIn(attribute, cookie)
                auth = {"Cookie": cookie.split(";", 1)[0]}
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    responses = list(pool.map(lambda _: request("/api/server/config", extra=auth), range(20)))
                self.assertTrue(all(r[0] == 200 for r in responses))
                with patch.object(server.RequestHandlerClass.backend.device, "request", return_value=(401, b"", "text/plain")):
                    status, headers, _ = request("/api/device/status", extra=auth)
                    self.assertEqual(status, 502)
                    self.assertNotIn("WWW-Authenticate", headers)
                    self.assertEqual(request("/health", extra=auth)[0], 200)
                status, _, _ = request("/api/security/logout", b"", {**auth, "Origin": "https://poko.example.com"})
                self.assertEqual(status, 303)
                status, headers, _ = request("/health", extra=auth)
                self.assertEqual(status, 401)
                self.assertNotIn("WWW-Authenticate", headers)
                # A browser's previously cached Basic credentials must not undo logout.
                cached = "Basic " + base64.b64encode(b"admin:correct horse test only").decode()
                self.assertEqual(request("/", extra={"Authorization": cached})[0], 303)
            finally:
                server.shutdown()
                server.server_close()
