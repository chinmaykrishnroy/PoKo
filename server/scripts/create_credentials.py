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
    password = secrets.token_urlsafe(24)
    token = secrets.token_urlsafe(32)
    values = {
        "POKO_ADMIN_USER": "admin",
        "POKO_ADMIN_PASSWORD_HASH": password_hash(password),
        "POKO_DEVICE_TOKEN_SHA256": hashlib.sha256(token.encode()).hexdigest(),
        "POKO_ADMIN_PASSWORD_HASH_FILE": str((directory / "admin-verifier.json").resolve()),
    }
    (directory / "credentials.env").write_text("".join(f"{name}='{value}'\n" for name, value in values.items()))
    (directory / "admin-password").write_text(password + "\n")
    (directory / "device-token").write_text(token + "\n")
    for path in directory.iterdir():
        os.chmod(path, 0o600)
    print("Credentials created in", directory, "; protect these files with your OS account permissions.")

if __name__ == "__main__":
    main()
