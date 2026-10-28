"""Configuration for opt-in live checks. Never execute dotenv contents."""
import argparse
import os
from pathlib import Path
import re
from urllib.parse import urlsplit


def verification_parser(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--env", type=Path, default=Path(os.environ.get("POKO_TEST_ENV", ".env")),
                        help="Private verification .env file (default: ./.env)")
    return parser


def load_verification_env(path):
    path = Path(path).resolve()
    values = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if not separator or not re.fullmatch(r"[A-Z][A-Z0-9_]*", key):
            raise ValueError(f"Invalid .env entry at line {number}")
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    # Process environment permits CI/operator overrides without editing the file.
    values.update({key: value for key, value in os.environ.items() if key.startswith("POKO_TEST_")})
    values["_directory"] = str(path.parent)
    return values


def target_url(values, key, *, https=False):
    value = values.get(key, "").rstrip("/")
    parsed = urlsplit(value)
    if (parsed.scheme not in ({"https"} if https else {"http", "https"}) or
            not parsed.hostname or parsed.username or parsed.password or
            parsed.path not in {"", "/"} or parsed.query or parsed.fragment):
        raise ValueError(f"Set {key} to a valid {'HTTPS ' if https else ''}origin in your .env file")
    return value


def secret_directory(values):
    if not values.get("POKO_TEST_SECRETS_DIR"):
        raise ValueError("Set POKO_TEST_SECRETS_DIR in your .env file")
    directory = Path(values["POKO_TEST_SECRETS_DIR"]).expanduser()
    return (Path(values["_directory"]) / directory).resolve()
