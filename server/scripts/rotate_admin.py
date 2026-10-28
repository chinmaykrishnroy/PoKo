"""Rotate only the Pi browser login; preserve device tokens/passwords."""
import os
from pathlib import Path
import secrets
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from server.poko_server.security import password_hash

root = Path(sys.argv[1] if len(sys.argv) > 1 else "/etc/poko")
env = root / "credentials.env"
password = secrets.token_urlsafe(24)
lines = [line for line in env.read_text().splitlines() if not line.startswith("POKO_ADMIN_PASSWORD_HASH=")]
lines.append("POKO_ADMIN_PASSWORD_HASH='" + password_hash(password) + "'")
temporary = env.with_suffix(".tmp")
temporary.write_text("\n".join(lines) + "\n")
os.chmod(temporary, 0o600)
os.replace(temporary, env)
recovery = root / "admin-password"
recovery.write_text(password + "\n")
os.chmod(recovery, 0o600)
print("Browser password rotated. Restart poko.service and read admin-password locally. Device credentials preserved.")
