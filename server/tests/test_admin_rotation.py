import itertools
import os
from pathlib import Path
import subprocess
import sys
import unittest

from helpers import workspace_tempdir


class AdminRotationTests(unittest.TestCase):
    def test_initial_setup_and_operator_bootstrap_recovery(self):
        from unittest.mock import patch
        from server.poko_server.security import Authentication
        with workspace_tempdir() as root:
            directory = root / "private"
            scripts = Path(__file__).parents[1] / "scripts"
            result = subprocess.run([sys.executable, str(scripts / "create_credentials.py"), str(directory)], capture_output=True, text=True, check=True)
            code = (directory / "setup-code").read_text().strip()
            self.assertNotIn(code, result.stdout + result.stderr)
            values = dict(line.split("=", 1) for line in (directory / "credentials.env").read_text().splitlines())
            values = {key: value.strip("'") for key, value in values.items()}
            with patch.dict(os.environ, values):
                self.assertTrue(Authentication(True, "https://poko.example.com").requires_password_change)
            integration = values["POKO_DEVICE_TOKEN_SHA256"]
            result = subprocess.run([sys.executable, str(scripts / "rotate_admin.py"), str(directory), "--bootstrap"], capture_output=True, text=True, check=True)
            self.assertEqual((directory / "admin-password").read_text().strip(), "00000000")
            self.assertNotEqual((directory / "setup-code").read_text().strip(), code)
            self.assertIn(integration, (directory / "credentials.env").read_text())
            self.assertNotIn(code, result.stdout + result.stderr)

    def test_memorable_rotation_preserves_integration_and_does_not_print_secret(self):
        with workspace_tempdir() as root:
            directory = root / "private"
            directory.mkdir()
            env = directory / "credentials.env"
            env.write_text("POKO_ADMIN_PASSWORD_HASH='old'\nPOKO_DEVICE_PASSWORD='keep-device'\nPOKO_DEVICE_TOKEN_SHA256='keep-token'\n")
            words = root / "words.txt"
            words.write_text("\n".join("word" + "".join(word) for word in itertools.product("abcdefghijklmnop", repeat=3)))
            script = Path(__file__).parents[1] / "scripts" / "rotate_admin.py"
            result = subprocess.run([sys.executable, str(script), str(directory), "--word-list", str(words)], capture_output=True, text=True, check=True)
            password = (directory / "admin-password").read_text().strip()
            self.assertEqual(len(password.split("-")), 6)
            self.assertNotIn(password, result.stdout + result.stderr)
            self.assertIn("POKO_DEVICE_PASSWORD='keep-device'", env.read_text())
            self.assertIn("POKO_DEVICE_TOKEN_SHA256='keep-token'", env.read_text())
            self.assertIn("scrypt$", env.read_text())
            if os.name != "nt":
                self.assertEqual(env.stat().st_mode & 0o777, 0o600)
