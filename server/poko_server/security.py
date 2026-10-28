"""Small, fail-closed HTTP authentication boundary for a tunnel origin.

Credentials deliberately live outside editable YAML and the configuration API.
Browser sessions use HttpOnly cookies; Basic/Bearer remain for API clients.
TLS belongs at the tunnel or the directly configured HTTPS listener.
"""
from __future__ import annotations

import base64
import binascii
import hashlib
import hmac
import os
import secrets
import threading
import time
import json
from pathlib import Path
import tempfile
from http.cookies import SimpleCookie, CookieError
from urllib.parse import urlsplit
from .device_tokens import DeviceTokens


def password_hash(password: str, salt: bytes | None = None) -> str:
    salt = salt or os.urandom(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=16384, r=8, p=1)
    return f"scrypt${salt.hex()}${digest.hex()}"


class Authentication:
    def __init__(self, enabled: bool, origin: str) -> None:
        self.enabled = enabled
        self.origin = origin
        self.username = os.environ.get("POKO_ADMIN_USER", "admin")
        self.encoded = os.environ.get("POKO_ADMIN_PASSWORD_HASH", "")
        self._base_encoded = self.encoded
        self.password_file = os.environ.get("POKO_ADMIN_PASSWORD_HASH_FILE", "")
        self.device_digest = os.environ.get("POKO_DEVICE_TOKEN_SHA256", "")
        self.tokens = DeviceTokens(os.environ.get("POKO_DEVICE_TOKENS_FILE", ""), self.device_digest)
        self.setup_code_file = os.environ.get("POKO_SETUP_CODE_FILE", "")
        self.requires_password_change = False
        self._sessions = {}
        self._session_lock = threading.Lock()
        self._failed_logins = []
        if enabled:
            parsed = urlsplit(origin)
            if parsed.scheme != "https" or not parsed.netloc or parsed.path or parsed.query or parsed.fragment:
                raise ValueError("security.public_origin must be an HTTPS origin without a path")
            if self.password_file and Path(self.password_file).exists():
                saved = json.loads(Path(self.password_file).read_text())
                if saved.get("base") == hashlib.sha256(self._base_encoded.encode()).hexdigest():
                    self.encoded = saved["hash"]
            try:
                algorithm, salt, digest = self.encoded.split("$")
                self.salt = bytes.fromhex(salt)
                self.digest = bytes.fromhex(digest)
                if algorithm != "scrypt" or len(self.salt) != 16 or len(self.digest) != 64:
                    raise ValueError()
            except ValueError:
                raise ValueError("Set a valid POKO_ADMIN_PASSWORD_HASH before enabling security") from None
            self.requires_password_change = hmac.compare_digest(
                hashlib.scrypt(b"00000000", salt=self.salt, n=16384, r=8, p=1), self.digest)

    def change_password(self, current: str, password: str, confirmation: str, setup_code: str = "") -> None:
        if not self.password_file:
            raise ValueError("Configure POKO_ADMIN_PASSWORD_HASH_FILE for browser password changes")
        if not 16 <= len(password) <= 128 or password != confirmation or password == current:
            raise ValueError("Use a different 16-128 character passphrase with matching confirmation")
        with self._session_lock:
            header = "Basic " + base64.b64encode((self.username + ":" + current).encode()).decode()
            if self.role(header) not in {"admin", "setup"}:
                raise ValueError("Current password is incorrect")
            if self.requires_password_change:
                path = Path(self.setup_code_file) if self.setup_code_file else None
                expected = path.read_text().strip() if path and path.exists() else ""
                if not expected or not hmac.compare_digest(expected, setup_code):
                    raise ValueError("Enter the one-time setup code from the server machine")
            encoded = password_hash(password)
            path = Path(self.password_file)
            descriptor, temporary = tempfile.mkstemp(prefix=".poko-password-", dir=path.parent)
            try:
                with os.fdopen(descriptor, "w") as stream:
                    json.dump({"base": hashlib.sha256(self._base_encoded.encode()).hexdigest(), "hash": encoded}, stream)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            self.encoded = encoded
            _, salt, digest = encoded.split("$")
            self.salt, self.digest = bytes.fromhex(salt), bytes.fromhex(digest)
            self.requires_password_change = False
            self._sessions.clear()

    def authorized(self, header: str) -> bool:
        return self.role(header) is not None

    def login(self, username: str, password: str, remember: bool) -> tuple[str | None, bool]:
        now = time.time()
        with self._session_lock:
            self._failed_logins = [t for t in self._failed_logins if now - t < 60]
            if len(self._failed_logins) >= 10:
                return None, True
        generation = self.encoded
        header = "Basic " + base64.b64encode((username + ":" + password).encode()).decode()
        if self.role(header) not in {"admin", "setup"}:
            with self._session_lock:
                self._failed_logins.append(now)
            return None, False
        token = secrets.token_urlsafe(32)
        with self._session_lock:
            if generation != self.encoded:
                return None, False
            self._sessions = {key: expiry for key, expiry in self._sessions.items() if expiry > now}
            if len(self._sessions) >= 64:
                del self._sessions[next(iter(self._sessions))]
            self._sessions[hashlib.sha256(token.encode()).digest()] = now + (30 * 86400 if remember else 12 * 3600)
            self._failed_logins.clear()
        return token, False

    def _cookie_token(self, header: str) -> str:
        if len(header) > 4096:
            return ""
        try:
            cookie = SimpleCookie()
            cookie.load(header)
            return cookie["poko_session"].value if "poko_session" in cookie else ""
        except CookieError:
            return ""

    def cookie_role(self, header: str) -> str | None:
        token = self._cookie_token(header)
        if not token:
            return None
        key = hashlib.sha256(token.encode()).digest()
        with self._session_lock:
            return ("setup" if self.requires_password_change else "admin") if self._sessions.get(key, 0) > time.time() else None

    def logout(self, header: str) -> None:
        token = self._cookie_token(header)
        with self._session_lock:
            self._sessions.pop(hashlib.sha256(token.encode()).digest(), None)

    def role(self, header: str) -> str | None:
        if not self.enabled:
            return "admin"
        try:
            scheme, encoded = header.split(" ", 1)
            if scheme.lower() == "bearer":
                return "device" if self.tokens.matches(encoded) else None
            if scheme.lower() != "basic" or len(encoded) > 2048:
                return None
            username, password = base64.b64decode(encoded, validate=True).decode().split(":", 1)
            digest = hashlib.scrypt(password.encode(), salt=self.salt, n=16384, r=8, p=1)
            return ("setup" if self.requires_password_change else "admin") if hmac.compare_digest(username.encode(), self.username.encode()) & hmac.compare_digest(digest, self.digest) else None
        except (ValueError, UnicodeError, binascii.Error):
            return None

    @staticmethod
    def device_route_allowed(method: str, path: str) -> bool:
        if method != "GET":
            return False
        if path in {"/health", "/api/playback/status", "/api/playback/operation",
                    "/api/playback/stop", "/api/playback/seek", "/api/playback/next", "/api/playback/previous"}:
            return True
        if path.startswith("/api/library/"):
            return True
        parts = path.strip("/").split("/")
        return len(parts) == 4 and parts[0] == "api" and (
            (parts[1] in {"audio", "video"} and parts[3] == "play") or
            (parts[1] == "image" and parts[3] in {"show", "jpeg"}))

    def same_origin(self, headers) -> bool:
        if not self.enabled:
            return True
        # Legacy API includes state-changing GETs, so enforce this for all
        # authenticated browser requests, not just POST/DELETE.
        if headers.get("Sec-Fetch-Site", "") == "cross-site":
            return False
        origin = headers.get("Origin")
        if origin and origin != self.origin:
            return False
        referer = headers.get("Referer")
        if referer:
            parsed = urlsplit(referer)
            if f"{parsed.scheme}://{parsed.netloc}" != self.origin:
                return False
        return True
