"""Rotate only the Pi browser login; preserve device tokens/passwords."""
import os
from pathlib import Path
import secrets
import sys
import argparse
import getpass
import re
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from server.poko_server.security import password_hash

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", nargs="?", type=Path, default=Path("/etc/poko"))
choices = parser.add_mutually_exclusive_group()
choices.add_argument("--prompt", action="store_true", help="Enter your own passphrase privately")
choices.add_argument("--word-list", type=Path, help="Generate six random words from a 4096+ word list")
args = parser.parse_args()
root = args.directory
env = root / "credentials.env"
if args.prompt:
    password = getpass.getpass("New browser passphrase (16-128 characters): ")
    if password != getpass.getpass("Confirm passphrase: ") or not 16 <= len(password) <= 128:
        parser.error("Use matching passphrases of 16-128 characters")
elif args.word_list:
    words = sorted({line.split()[-1].lower() for line in args.word_list.read_text().splitlines()
                    if line.split() and re.fullmatch(r"[A-Za-z]{3,12}", line.split()[-1])})
    if len(words) < 4096:
        parser.error("Provide at least 4096 distinct words")
    password = "-".join(secrets.choice(words) for _ in range(6))
else:
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
