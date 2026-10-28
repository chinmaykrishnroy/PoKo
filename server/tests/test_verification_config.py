import os
import unittest
from unittest.mock import patch

from helpers import workspace_tempdir
from server.deploy.verification_config import load_verification_env, target_url, secret_directory


class VerificationConfigTests(unittest.TestCase):
    def test_private_file_paths_and_process_overrides(self):
        with workspace_tempdir() as root, patch.dict(os.environ, {}, clear=True):
            path = root / "targets.env"
            path.write_text('# comment\nPOKO_TEST_SERVER_URL="https://poko.example.com"\n'
                            "POKO_TEST_SECRETS_DIR=private-secrets\n", encoding="utf-8")
            values = load_verification_env(path)
            self.assertEqual(target_url(values, "POKO_TEST_SERVER_URL", https=True), "https://poko.example.com")
            self.assertEqual(secret_directory(values), (root / "private-secrets").resolve())
            with patch.dict(os.environ, {"POKO_TEST_SERVER_URL": "https://override.example.com"}):
                self.assertEqual(load_verification_env(path)["POKO_TEST_SERVER_URL"], "https://override.example.com")

    def test_refuses_missing_unsafe_or_plaintext_public_targets(self):
        for value in ("", "http://example.com", "https://user:secret@example.com", "https://example.com/path", "https://example.com?secret=value"):
            with self.assertRaises(ValueError):
                target_url({"target": value}, "target", https=True)

    def test_dotenv_is_data_not_shell_code(self):
        with workspace_tempdir() as root, patch.dict(os.environ, {}, clear=True):
            path = root / "targets.env"
            path.write_text("POKO_TEST_SECRETS_DIR='$(do-not-execute)'\n", encoding="utf-8")
            self.assertEqual(load_verification_env(path)["POKO_TEST_SECRETS_DIR"], "$(do-not-execute)")
            path.write_text("not an assignment\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                load_verification_env(path)
