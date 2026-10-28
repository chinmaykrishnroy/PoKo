import itertools
import os
from pathlib import Path
import subprocess
import sys
import unittest

from helpers import workspace_tempdir


class AdminRotationTests(unittest.TestCase):
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
