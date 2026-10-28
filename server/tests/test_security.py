from __future__ import annotations

import base64
import hashlib
import json
import os
import shutil
import ssl
import subprocess
import threading
import unittest
import urllib.error
import urllib.request
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from helpers import workspace_tempdir
from server.poko_server.config import LibraryConfig, SecurityConfig, load_config
from server.poko_server.http_api import make_server
from server.poko_server.security import Authentication, password_hash


class SecurityTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("openssl"), "openssl required for local TLS integration")
    def test_local_tls_with_private_ca_and_device_token(self):
        with workspace_tempdir() as root:
            key, cert = root / "server.key", root / "ca.pem"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                            "-keyout", str(key), "-out", str(cert), "-days", "1",
                            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                           check=True, capture_output=True)
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0,
                             security=SecurityConfig(True, "https://localhost:8765"),
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "db"))
            with patch.dict(os.environ, {"POKO_ADMIN_PASSWORD_HASH": password_hash("local-test"),
                                         "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(b"local-token").hexdigest(),
                                         "POKO_TLS_CERT": str(cert), "POKO_TLS_KEY": str(key)}):
                server = make_server(config, dry_run=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                request = urllib.request.Request(f"https://localhost:{server.server_address[1]}/health",
                                                 headers={"Authorization": "Bearer local-token"})
                with urllib.request.urlopen(request, context=ssl.create_default_context(cafile=str(cert))) as response:
                    self.assertTrue(json.load(response)["ok"])
                with self.assertRaises(urllib.error.URLError):
                    urllib.request.urlopen(request, context=ssl.create_default_context())
            finally:
                server.shutdown()
                server.server_close()

    def test_fail_closed_without_credentials(self):
        with patch.dict(os.environ, {"POKO_ADMIN_PASSWORD_HASH": ""}):
            with self.assertRaises(ValueError):
                Authentication(True, "https://poko.example.com")

    def test_authentication_and_origin_boundary_over_http(self):
        with workspace_tempdir() as root, patch.dict(os.environ, {
            "POKO_ADMIN_USER": "admin",
            "POKO_ADMIN_PASSWORD_HASH": password_hash("test-password-only"),
            "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(b"test-device-token").hexdigest(),
        }):
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0,
                             security=SecurityConfig(True, "https://poko.example.com"),
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "index.db"))
            server = make_server(config, dry_run=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            url = f"http://127.0.0.1:{server.server_address[1]}"
            auth = "Basic " + base64.b64encode(b"admin:test-password-only").decode()
            try:
                for path in ["/", "/health", "/api/server/config", "/api/device/power?power_off=1"]:
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(url + path)
                    self.assertEqual(error.exception.code, 401)
                request = urllib.request.Request(url + "/health", headers={"Authorization": auth})
                with urllib.request.urlopen(request) as response:
                    self.assertTrue(json.load(response)["ok"])
                    self.assertIsNone(response.headers.get("Access-Control-Allow-Origin"))
                    self.assertEqual(response.headers["Cache-Control"], "no-store")
                for path in ["/api/server/config", "/api/device/power?power_off=1", "/"]:
                    request = urllib.request.Request(url + path, headers={"Authorization": "Bearer test-device-token"})
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(request)
                    self.assertEqual(error.exception.code, 403)
                request = urllib.request.Request(url + "/api/library/audio?icons=false", headers={"Authorization": "Bearer test-device-token"})
                with urllib.request.urlopen(request) as response:
                    self.assertEqual(response.status, 200)
                for headers in [{"Origin": "https://evil.invalid"}, {"Sec-Fetch-Site": "cross-site"},
                                {"Referer": "https://evil.invalid/page"}]:
                    request = urllib.request.Request(url + "/health", headers={"Authorization": auth, **headers})
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(request)
                    self.assertEqual(error.exception.code, 403)
            finally:
                server.shutdown()
                server.server_close()

    def test_malformed_and_wrong_credentials(self):
        with patch.dict(os.environ, {"POKO_ADMIN_PASSWORD_HASH": password_hash("correct-password")}):
            auth = Authentication(True, "https://poko.example.com")
            for header in ["", "Bearer anything", "Basic !!!", "Basic " + base64.b64encode(b"admin:wrong").decode()]:
                self.assertFalse(auth.authorized(header))
            self.assertFalse(auth.same_origin({"Origin": "null"}))

    def test_secure_listener_cannot_be_public(self):
        config = load_config(Path(__file__).parents[1] / "config.example.yml")
        config = replace(config, host="0.0.0.0", security=SecurityConfig(True, "https://poko.example.com"))
        with self.assertRaisesRegex(ValueError, "loopback"):
            make_server(config)
