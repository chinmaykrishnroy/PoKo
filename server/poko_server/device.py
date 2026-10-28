from __future__ import annotations

import time
import json
import os
import threading
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass

from .config import AppConfig


@dataclass
class DeviceResponse:
    ok: bool
    app: str
    body: str = ""
    error: str | None = None


class DeviceClient:
    def __init__(self, config: AppConfig, enabled: bool = True) -> None:
        self.config = config
        self.enabled = enabled
        # ESP32 WebServer has one Digest nonce. Serialize each challenge/response
        # exchange so parallel dashboard requests cannot invalidate one another.
        self._auth_lock = threading.Lock()

    def _open(self, request, timeout):
        with self._auth_lock:
            return self._open_authenticated(request, timeout)

    def _open_authenticated(self, request, timeout):
        password = os.environ.get("POKO_DEVICE_PASSWORD", "")
        if not password:
            return urllib.request.urlopen(request, timeout=timeout)
        manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        manager.add_password(None, self.config.poko.base_url, "admin", password)
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
        return opener.open(request, timeout=timeout)

    def switch(self, app: str) -> DeviceResponse:
        if not self.enabled:
            return DeviceResponse(ok=True, app=app, body="dry-run")
        query = urllib.parse.urlencode({"set": app})
        url = f"{self.config.poko.base_url}/api/app?{query}"
        return self._get(url, app)

    def notify_playback_stopped(self) -> DeviceResponse:
        if not self.enabled:
            return DeviceResponse(ok=True, app="playback_stopped", body="dry-run")
        url = f"{self.config.poko.base_url}/api/ui/playback_stopped"
        return self._get(url, "playback_stopped")

    def request(
        self,
        path: str,
        *,
        method: str = "GET",
        data: bytes | None = None,
        content_type: str = "application/octet-stream",
        timeout: float = 6,
    ) -> tuple[int, bytes, str]:
        if not self.enabled:
            payload = json.dumps({"ok": False, "error": "device access disabled in dry-run mode"}).encode("utf-8")
            return 503, payload, "application/json"
        url = f"{self.config.poko.base_url}/{path.lstrip('/')}"
        headers = {"Content-Type": content_type} if data is not None else {}
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with self._open(request, timeout) as response:
                return response.status, response.read(), response.headers.get_content_type()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read(), exc.headers.get_content_type()
        except Exception as exc:
            payload = json.dumps({"ok": False, "error": str(exc)}).encode("utf-8")
            return 503, payload, "application/json"
    def _get(self, url: str, app: str) -> DeviceResponse:
        try:
            with self._open(url, 3.0) as response:
                body = response.read().decode("utf-8", errors="replace")
            payload = json.loads(body)
            if not isinstance(payload, dict) or payload.get("ok") is not True:
                return DeviceResponse(ok=False, app=app, body=body, error="device rejected app switch")
            time.sleep(max(0, self.config.poko.switch_delay_ms) / 1000)
            return DeviceResponse(ok=True, app=app, body=body)
        except Exception:
            return DeviceResponse(ok=False, app=app, error="device unreachable or authentication failed")

