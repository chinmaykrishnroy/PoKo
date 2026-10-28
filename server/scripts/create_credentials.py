"""Create local-server credentials without printing secrets or overwriting files."""
import hashlib
import os
from pathlib import Path
import secrets
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from server.poko_server.security import password_hash

def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "server-secrets")
    directory.mkdir(mode=0o700)
    password = "00000000"
    token = secrets.token_urlsafe(32)
    values = {
        "POKO_ADMIN_USER": "admin",
        "POKO_ADMIN_PASSWORD_HASH": password_hash(password),
        "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(token.encode()).hexdigest(),
        "POKO_ADMIN_PASSWORD_HASH_FILE": str((directory / "admin-verifier.json").resolve()),
        "POKO_DEVICE_TOKENS_FILE": str((directory / "device-tokens.json").resolve()),
        "POKO_SETUP_CODE_FILE": str((directory / "setup-code").resolve()),
    }
    (directory / "credentials.env").write_text("".join(f"{name}='{value}'\n" for name, value in values.items()))
    (directory / "admin-password").write_text(password + "\n")
    (directory / "device-token").write_text(token + "\n")
    (directory / "setup-code").write_text(secrets.token_urlsafe(24) + "\n")
    for path in directory.iterdir():
        os.chmod(path, 0o600)
    print("Credentials created in", directory, "; initial login admin / 00000000. Read setup-code locally to claim setup, then choose a private passphrase.")

if __name__ == "__main__":
    main()
