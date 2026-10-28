import base64
import hashlib
import json
import os
from pathlib import Path
import threading
import unittest
import urllib.request
import urllib.error
from dataclasses import replace
from unittest.mock import patch

from helpers import workspace_tempdir
from server.poko_server.device_tokens import DeviceTokens
from server.poko_server.security import Authentication, password_hash
from server.poko_server.config import LibraryConfig, SecurityConfig, load_config, config_to_yaml
from server.poko_server.http_api import make_server


def basic(password):
    return "Basic " + base64.b64encode(("admin:" + password).encode()).decode()


class DeviceTokenTests(unittest.TestCase):
    def test_tokens_are_hash_only_revocable_durable_and_bounded(self):
        with workspace_tempdir() as root:
            path = root / "tokens.json"
            legacy = hashlib.sha256(b"legacy-token").hexdigest()
            store = DeviceTokens(str(path), legacy)
            created = store.create("Desk PoKo")
            self.assertTrue(store.matches(created["token"]))
            self.assertNotIn(created["token"], path.read_text())
            self.assertNotIn("hash", json.dumps(store.list()))
            self.assertNotIn("token", store.list()[-1])
            restarted = DeviceTokens(str(path), legacy)
            self.assertTrue(restarted.matches(created["token"]))
            restarted.revoke(created["id"])
            restarted.revoke("legacy")
            again = DeviceTokens(str(path), legacy)
            self.assertFalse(again.matches(created["token"]))
            self.assertFalse(again.matches("legacy-token"))
            for index in range(32):
                again.create(str(index))
            with self.assertRaises(ValueError):
                again.create("too many")
            if os.name != "nt":
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_store_failure_does_not_authorize_unpersisted_token(self):
        store = DeviceTokens("")
        with self.assertRaises(ValueError):
            store.create("device")
        self.assertEqual(store.list(), [])

    def test_default_requires_private_claim_then_password_change_survives_restart(self):
        with workspace_tempdir() as root:
            code = root / "setup-code"
            code.write_text("test-claim-code")
            values = {"POKO_ADMIN_PASSWORD_HASH": password_hash("00000000"),
                      "POKO_ADMIN_PASSWORD_HASH_FILE": str(root / "admin.json"),
                      "POKO_SETUP_CODE_FILE": str(code)}
            with patch.dict(os.environ, values):
                auth = Authentication(True, "https://poko.example.com")
                self.assertEqual(auth.role(basic("00000000")), "setup")
                cookie, _ = auth.login("admin", "00000000", False)
                self.assertEqual(auth.cookie_role("poko_session=" + cookie), "setup")
                with self.assertRaises(ValueError):
                    auth.change_password("00000000", "my new server password", "my new server password", "wrong")
                auth.change_password("00000000", "my new server password", "my new server password", "test-claim-code")
                self.assertIsNone(auth.cookie_role("poko_session=" + cookie))
                restarted = Authentication(True, "https://poko.example.com")
                self.assertFalse(restarted.requires_password_change)
                self.assertEqual(restarted.role(basic("my new server password")), "admin")
                self.assertIsNone(restarted.role(basic("00000000")))

    def test_http_bootstrap_blocks_controls_and_tokens_are_media_only(self):
        with workspace_tempdir() as root, patch.dict(os.environ, {
            "POKO_ADMIN_PASSWORD_HASH": password_hash("00000000"),
            "POKO_ADMIN_PASSWORD_HASH_FILE": str(root / "admin.json"),
            "POKO_SETUP_CODE_FILE": str(root / "setup-code"),
            "POKO_DEVICE_TOKENS_FILE": str(root / "tokens.json"),
            "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(b"existing-device").hexdigest(),
        }):
            (root / "setup-code").write_text("private-claim-code")
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0, security=SecurityConfig(True, "https://poko.example.com"),
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "index.db"))
            path = root / "config.yml"
            path.write_text(config_to_yaml(config))
            server = make_server(config, dry_run=True, config_path=path)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            base = f"http://127.0.0.1:{server.server_address[1]}"
            def request(path, password="00000000", data=None, token=None, origin="https://poko.example.com"):
                headers = {"Authorization": "Bearer " + token if token else basic(password), "Origin": origin}
                if data is not None:
                    headers["Content-Type"] = "application/json"
                req = urllib.request.Request(base + path, data=json.dumps(data).encode() if data is not None else None, headers=headers)
                try:
                    response = urllib.request.urlopen(req, timeout=10)
                except urllib.error.HTTPError as error:
                    response = error
                with response:
                    return response.status, response.read()
            try:
                self.assertEqual(request("/setup")[0], 200)
                for endpoint in ("/health", "/api/server/config", "/api/security/device-tokens"):
                    self.assertEqual(request(endpoint)[0], 403)
                self.assertEqual(request("/health", token="existing-device")[0], 403)
                change = {"current_password": "00000000", "password": "new private passphrase", "confirmation": "new private passphrase"}
                self.assertEqual(request("/api/security/password", data=change)[0], 400)
                change["setup_code"] = "private-claim-code"
                self.assertEqual(request("/api/security/password", data=change)[0], 200)
                new = "new private passphrase"
                self.assertEqual(request("/api/security/device-tokens", password=new, data={"name": "Desk"}, origin="https://other.example.com")[0], 403)
                status, payload = request("/api/security/device-tokens", password=new, data={"name": "Desk"})
                self.assertEqual(status, 201)
                created = json.loads(payload)
                self.assertEqual(request("/health", token=created["token"])[0], 200)
                self.assertEqual(request("/api/security/device-tokens", token=created["token"])[0], 403)
                self.assertEqual(request("/api/server/config", token=created["token"])[0], 403)
                self.assertNotIn(created["token"].encode(), request("/api/security/device-tokens", password=new)[1])
                self.assertEqual(request("/api/security/device-tokens", password=new, data={"action": "revoke", "id": created["id"]})[0], 200)
                self.assertEqual(request("/health", token=created["token"])[0], 401)
            finally:
                server.shutdown()
                server.server_close()
