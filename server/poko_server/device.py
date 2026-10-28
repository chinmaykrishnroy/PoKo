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
from .models import MediaItem


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
        deadline = time.monotonic() + timeout
        if not self._auth_lock.acquire(timeout=max(0, timeout)):
            raise TimeoutError("device authentication is busy")
        try:
            return self._open_authenticated(request, max(0.001, deadline - time.monotonic()))
        finally:
            self._auth_lock.release()

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

    def prepare_media(self, item: MediaItem, start_s: float = 0, *, catalog_index: int = 0, catalog_total: int = 0) -> DeviceResponse:
        app = "audio" if item.kind == "audio" else "sync"
        if not self.enabled:
            return DeviceResponse(ok=True, app=app, body="dry-run")
        payload = {"kind": item.kind, "id": item.id, "title": item.title[:128],
                   "artist": (item.artist or "Unknown Artist")[:128],
                   "duration_s": max(0, int(item.duration_s or 0)), "position_s": max(0, int(start_s)),
                   "catalog_index": catalog_index, "catalog_total": catalog_total}
        status, body, _ = self.request("/api/playback/prepare", method="POST",
                                       data=json.dumps(payload, ensure_ascii=False).encode(), content_type="application/json", timeout=15)
        if status == 404:
            return DeviceResponse(ok=False, app=app, error="Update PoKo firmware to support synchronized remote playback")
        try:
            acknowledged = json.loads(body)
            if status != 200 or acknowledged.get("ok") is not True or acknowledged.get("media_id") != item.id:
                return DeviceResponse(ok=False, app=app, error="device did not acknowledge the selected media")
        except (ValueError, AttributeError):
            return DeviceResponse(ok=False, app=app, error="invalid device playback acknowledgement")
        time.sleep(max(0, self.config.poko.switch_delay_ms) / 1000)
        return DeviceResponse(ok=True, app=app, body="metadata synchronized")

    def notify_playback_stopped(self) -> DeviceResponse:
        if not self.enabled:
            return DeviceResponse(ok=True, app="playback_stopped", body="dry-run")
        url = f"{self.config.poko.base_url}/api/ui/playback_stopped?reason=stop"
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

