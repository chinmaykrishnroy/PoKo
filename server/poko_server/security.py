"""Small, fail-closed HTTP authentication boundary for a tunnel origin.

Credentials deliberately live outside editable YAML and the configuration API.
Basic authentication provides the browser's native sign-in prompt; TLS belongs
at Cloudflare. The origin is restricted to loopback in secured deployments.
"""
from __future__ import annotations

import base64
import binascii
import hashlib
import hmac
import os
from urllib.parse import urlsplit


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
        self.device_digest = os.environ.get("POKO_DEVICE_TOKEN_SHA256", "")
        if enabled:
            parsed = urlsplit(origin)
            if parsed.scheme != "https" or not parsed.netloc or parsed.path or parsed.query or parsed.fragment:
                raise ValueError("security.public_origin must be an HTTPS origin without a path")
            try:
                algorithm, salt, digest = self.encoded.split("$")
                self.salt = bytes.fromhex(salt)
                self.digest = bytes.fromhex(digest)
                if algorithm != "scrypt" or len(self.salt) != 16 or len(self.digest) != 64:
                    raise ValueError()
            except ValueError:
                raise ValueError("Set a valid POKO_ADMIN_PASSWORD_HASH before enabling security") from None

    def authorized(self, header: str) -> bool:
        return self.role(header) is not None

    def role(self, header: str) -> str | None:
        if not self.enabled:
            return "admin"
        try:
            scheme, encoded = header.split(" ", 1)
            if scheme.lower() == "bearer":
                if not self.device_digest or len(encoded) > 256:
                    return None
                digest = hashlib.sha256(encoded.encode()).hexdigest()
                return "device" if hmac.compare_digest(digest, self.device_digest) else None
            if scheme.lower() != "basic" or len(encoded) > 2048:
                return None
            username, password = base64.b64decode(encoded, validate=True).decode().split(":", 1)
            digest = hashlib.scrypt(password.encode(), salt=self.salt, n=16384, r=8, p=1)
            return "admin" if hmac.compare_digest(username.encode(), self.username.encode()) & hmac.compare_digest(digest, self.digest) else None
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
