"""Run as root after copying the server package to /opt/poko/server.

Generates credentials once, never prints them. This is a deployment script,
not an HTTP endpoint. Change credentials by removing the env file and rerunning.
"""
import os
from pathlib import Path
import secrets
import hashlib
import sys

sys.path.insert(0, "/opt/poko")
from server.poko_server.config import load_config, config_to_yaml, SecurityConfig, LibraryConfig
from server.poko_server.security import password_hash
from dataclasses import replace

secrets_dir = Path("/etc/poko")
secrets_dir.mkdir(mode=0o700, exist_ok=True)
env = secrets_dir / "credentials.env"
if not env.exists():
    password = secrets.token_urlsafe(24)
    with env.open("x") as stream:
        stream.write("POKO_ADMIN_USER=admin\nPOKO_ADMIN_PASSWORD_HASH='" + password_hash(password) + "'\n")
    os.chmod(env, 0o600)
    password_file = secrets_dir / "admin-password"
    with password_file.open("x") as stream:
        stream.write(password + "\n")
    os.chmod(password_file, 0o600)

for variable, filename, hashed in [
    ("POKO_DEVICE_TOKEN_SHA256", "device-token", True),
    ("POKO_DEVICE_PASSWORD", "device-admin-password", False),
]:
    if variable + "=" not in env.read_text():
        credential = secrets.token_urlsafe(32)
        value = hashlib.sha256(credential.encode()).hexdigest() if hashed else credential
        with env.open("a") as stream:
            stream.write(f"{variable}='{value}'\n")
        secret_file = secrets_dir / filename
        secret_file.write_text(credential + "\n")
        os.chmod(secret_file, 0o600)

config_path = Path("/var/lib/poko/config.yml")
if not config_path.exists():
    config = load_config("/opt/poko/server/config.example.yml")
    config = replace(config, host="127.0.0.1", port=8765,
                     security=SecurityConfig(True, "https://poko.prefect-sys.online"),
                     library=LibraryConfig([Path("/srv/poko/music"), Path("/srv/poko/movies")],
                                           Path("/var/lib/poko/uploads"), 10,
                                           db_path=Path("/var/lib/poko/catalog.db")))
    config_path.write_text(config_to_yaml(config))
    os.chmod(config_path, 0o600)
print("Configuration ready; credentials stored only in /etc/poko")
