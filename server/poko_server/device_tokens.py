"""Revocable media-only credentials. Raw tokens are returned exactly once."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import tempfile
import threading
import time


class DeviceTokens:
    def __init__(self, filename: str, legacy_digest: str = ""):
        self.path = Path(filename) if filename else None
        self.legacy_digest = legacy_digest
        self.lock = threading.RLock()
        self.state = {"version": 1, "legacy_revoked": False, "tokens": []}
        if self.path and self.path.exists():
            saved = json.loads(self.path.read_text())
            if (saved.get("version") != 1 or not isinstance(saved.get("legacy_revoked"), bool) or
                    not isinstance(saved.get("tokens"), list) or len(saved["tokens"]) > 32):
                raise ValueError("Invalid device token store")
            for item in saved["tokens"]:
                if (not all(isinstance(item.get(k), str) for k in ("id", "name", "hash")) or
                        len(item["hash"]) != 64 or not isinstance(item.get("created"), int)):
                    raise ValueError("Invalid device token record")
            self.state = saved

    def _save(self, state):
        if not self.path:
            raise ValueError("Configure POKO_DEVICE_TOKENS_FILE to manage device tokens")
        descriptor, temporary = tempfile.mkstemp(prefix=".poko-tokens-", dir=self.path.parent)
        try:
            with os.fdopen(descriptor, "w") as stream:
                json.dump(state, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        self.state = state

    def list(self):
        with self.lock:
            items = [{k: item[k] for k in ("id", "name", "created")} for item in self.state["tokens"]]
            if self.legacy_digest and not self.state["legacy_revoked"]:
                items.insert(0, {"id": "legacy", "name": "Original device token", "created": None})
            return items

    def create(self, name):
        if not isinstance(name, str) or not 1 <= len(name.strip()) <= 64:
            raise ValueError("Use a device name of 1-64 characters")
        with self.lock:
            if len(self.state["tokens"]) >= 32:
                raise ValueError("Revoke an unused device token before creating another")
            token = "poko_" + secrets.token_urlsafe(32)
            item = {"id": secrets.token_hex(8), "name": name.strip(), "created": int(time.time()),
                    "hash": hashlib.sha256(token.encode()).hexdigest()}
            self._save({**self.state, "tokens": [*self.state["tokens"], item]})
            return {"id": item["id"], "name": item["name"], "token": token}

    def revoke(self, identity):
        with self.lock:
            if identity == "legacy" and self.legacy_digest:
                self._save({**self.state, "legacy_revoked": True})
            elif any(item["id"] == identity for item in self.state["tokens"]):
                self._save({**self.state, "tokens": [item for item in self.state["tokens"] if item["id"] != identity]})
            else:
                raise ValueError("Device token not found")

    def matches(self, token):
        if not 1 <= len(token) <= 256:
            return False
        digest = hashlib.sha256(token.encode()).hexdigest()
        with self.lock:
            matched = bool(self.legacy_digest and not self.state["legacy_revoked"] and
                           hmac.compare_digest(digest, self.legacy_digest))
            for item in self.state["tokens"]:
                matched |= hmac.compare_digest(digest, item["hash"])
            return matched
