from __future__ import annotations

import argparse
import base64
import binascii
import json
import re
import subprocess
import time
from dataclasses import replace
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, unquote, urlencode, urlparse

from .config import DEFAULT_CONFIG_PATH, AppConfig, config_to_yaml, load_config, load_raw_config
from .device import DeviceClient
from .ffmpeg_tools import default_ffmpeg_executable, generic_icon, thumbnail_icon, thumbnail_raw_jpeg
from .media_index import MediaIndex
from .models import MediaItem
from .playback import PlaybackManager
from .ui import ADMIN_HTML, FAVICON_SVG


class NexusHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = False
    daemon_threads = True


class NexusBackend:
    def __init__(self, config: AppConfig, *, dry_run: bool = False, config_path: Path = DEFAULT_CONFIG_PATH) -> None:
        self.config = config
        self.config_path = config_path
        self.dry_run = dry_run
        self.started_at = time.time()
        self.index = MediaIndex(config)
        self.device = DeviceClient(config, enabled=not dry_run)
        self.playback = PlaybackManager(config, self.index, self.device, dry_run=dry_run)
        started = self.index.start_background_scan()
        print(f"Background indexer {'started' if started else 'already running'}", flush=True)

    def page_json(
        self,
        kind: str,
        page: int,
        *,
        include_icons: bool = True,
        enrich: bool = True,
        icon_size: int = 28,
        page_size: int | None = None,
    ) -> dict[str, Any]:
        page_data = self.index.page(kind, page, page_size=page_size)
        items: list[MediaItem] = page_data.pop("items")
        rendered: list[dict[str, Any]] = []
        removed_invalid = 0
        for item in items:
            data = self._item_json(item, include_icons=include_icons, enrich=enrich, icon_size=icon_size)
            if data is None:
                removed_invalid += 1
                continue
            rendered.append(data)
        if removed_invalid:
            refreshed = self.index.page(kind, page, page_size=page_size)
            for key in ("total", "total_pages", "empty"):
                page_data[key] = refreshed[key]
        return {
            **page_data,
            "items": rendered,
        }

    def _icon_for_item(self, item: MediaItem, icon_size: int = 28):
        if item.kind in {"audio", "video", "image"}:
            return thumbnail_icon(item.path, item.kind, self.config, size=icon_size)
        return generic_icon("text", icon_size)

    def _thumbnail_jpeg_for_item(self, item: MediaItem, size: int = 80) -> bytes | None:
        return thumbnail_raw_jpeg(item.path, item.kind, self.config, size=size)

    def _item_json(
        self,
        item: MediaItem,
        *,
        include_icons: bool = True,
        enrich: bool = True,
        icon_size: int = 28,
    ) -> dict[str, Any] | None:
        selected = self.index.enrich(item) if enrich else item
        if enrich and self.index.is_unplayable(selected):
            self.index.delete(selected.id)
            return None
        icon = self._icon_for_item(selected, icon_size) if include_icons else None
        data = selected.to_json(icon=icon)
        if selected.kind in {"video", "text", "image"}:
            data["relative_path"] = self._relative_path(selected.path)
        return data

    def _relative_path(self, path: Path) -> str:
        for root in self.config.library.read_folders:
            try:
                return str(path.relative_to(root))
            except ValueError:
                continue
        return path.name

    def status_json(self) -> dict[str, Any]:
        return {
            "ok": True,
            "dry_run": self.dry_run,
            "uptime_s": round(time.time() - self.started_at, 1),
            "config": self.config_summary(),
            "index": self.index.status(),
            "playback": self.playback.status(),
        }

    def config_summary(self) -> dict[str, Any]:
        return {
            "server": {"host": self.config.host, "port": self.config.port},
            "nexus": {"ip": self.config.nexus.ip, "base_url": self.config.nexus.base_url},
            "library": {
                "read_folders": [str(p) for p in self.config.library.read_folders],
                "write_folder": str(self.config.library.write_folder),
                "page_size": self.config.library.page_size,
                "probe_on_scan": self.config.library.probe_on_scan,
                "db_path": str(self.config.library.db_path),
            },
            "media": {
                "audio_bitrate": self.config.ffmpeg.audio_bitrate,
                "video_fps": self.config.ffmpeg.video_fps,
                "video_quality": self.config.ffmpeg.video_quality,
                "graphics_fps": self.config.ffmpeg.graphics_fps,
                "graphics_quality": self.config.ffmpeg.graphics_quality,
                "audio_filters": self.audio_filters_json(),
            },
        }

    def audio_filters_json(self) -> dict[str, Any]:
        return {
            "highpass": {
                "enabled": self.config.ffmpeg.highpass_enabled,
                "cutoff_hz": self.config.ffmpeg.highpass_hz,
                "min_hz": 20,
                "max_hz": 2000,
            },
            "lowpass": {
                "enabled": self.config.ffmpeg.lowpass_enabled,
                "cutoff_hz": self.config.ffmpeg.lowpass_hz,
                "min_hz": 1000,
                "max_hz": 20000,
            },
        }

    def update_audio_filters(self, body: dict[str, Any]) -> dict[str, Any]:
        ffmpeg = replace(
            self.config.ffmpeg,
            highpass_enabled=bool(body.get("highpass_enabled", self.config.ffmpeg.highpass_enabled)),
            highpass_hz=max(20, min(2000, int(body.get("highpass_hz", self.config.ffmpeg.highpass_hz)))),
            lowpass_enabled=bool(body.get("lowpass_enabled", self.config.ffmpeg.lowpass_enabled)),
            lowpass_hz=max(1000, min(20000, int(body.get("lowpass_hz", self.config.ffmpeg.lowpass_hz)))),
        )
        if ffmpeg.highpass_enabled and ffmpeg.lowpass_enabled and ffmpeg.highpass_hz >= ffmpeg.lowpass_hz:
            raise ValueError("high-pass cutoff must be below low-pass cutoff")
        updated = replace(self.config, ffmpeg=ffmpeg)
        self.config_path.write_text(config_to_yaml(updated), encoding="utf-8")
        self.reload_config()
        return self.audio_filters_json()

    def reload_config(self) -> None:
        self.config = load_config(self.config_path)
        self.index.config = self.config
        self.device.config = self.config
        self.playback.config = self.config

    @property
    def camera_dir(self) -> Path:
        path = self.config.library.write_folder / "camera"
        path.mkdir(parents=True, exist_ok=True)
        return path

    def camera_items(self, limit: int = 20) -> list[dict[str, Any]]:
        files = sorted(self.camera_dir.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
        return [
            {"name": p.name, "size_bytes": p.stat().st_size, "modified_ts": p.stat().st_mtime}
            for p in files[:limit]
        ]

    def camera_preview_jpeg(self, name: str) -> bytes | None:
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        path = self.camera_dir / safe
        if not path.exists() or path.suffix.lower() not in {".jpg", ".jpeg"}:
            return None
        ffmpeg = default_ffmpeg_executable(self.config.ffmpeg.executable)
        cmd = [
            ffmpeg, "-hide_banner", "-loglevel", "error",
            "-i", str(path),
            "-vf", "scale=240:240:force_original_aspect_ratio=increase,crop=240:240,format=yuvj420p",
            "-frames:v", "1",
            "-f", "image2pipe",
            "-vcodec", "mjpeg",
            "-q:v", "5",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=6, check=False)
            return result.stdout if result.returncode == 0 and result.stdout else path.read_bytes()
        except (OSError, subprocess.TimeoutExpired):
            return path.read_bytes()

    def image_preview_jpeg(self, item_id: str, aspect: str = "square") -> bytes | None:
        item = self.index.get(item_id)
        if not item or item.kind != "image":
            return None
        ffmpeg = default_ffmpeg_executable(self.config.ffmpeg.executable)
        if aspect == "fit":
            vf = "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,format=yuvj420p"
        else:
            vf = "scale=240:240:force_original_aspect_ratio=increase,crop=240:240,format=yuvj420p"
        cmd = [
            ffmpeg, "-hide_banner", "-loglevel", "error",
            "-i", str(item.path),
            "-vf", vf,
            "-frames:v", "1",
            "-f", "image2pipe",
            "-vcodec", "mjpeg",
            "-q:v", "3",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=8, check=False)
            return result.stdout if result.returncode == 0 and result.stdout else None
        except (OSError, subprocess.TimeoutExpired):
            return None


def _first(query: dict[str, list[str]], key: str, default: str | None = None) -> str | None:
    values = query.get(key)
    return values[0] if values else default


def _float_query(query: dict[str, list[str]], key: str, default: float = 0) -> float:
    value = _first(query, key)
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def _int_query(query: dict[str, list[str]], key: str, default: int = 0) -> int:
    value = _first(query, key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


class NexusRequestHandler(BaseHTTPRequestHandler):
    backend: NexusBackend

    server_version = "NexusBackend/0.1"

    def log_message(self, format: str, *args: Any) -> None:
        print("%s - - [%s] %s" % (self.client_address[0], self.log_date_time_string(), format % args))

    def do_GET(self) -> None:
        self._handle_logged()

    def do_POST(self) -> None:
        self._handle_logged()

    def do_DELETE(self) -> None:
        self._handle_logged()

    def _handle_logged(self) -> None:
        start = time.time()
        self._last_status = 200
        error = None
        try:
            self._handle()
        except Exception as exc:
            error = str(exc)
            self._json({"ok": False, "error": error}, HTTPStatus.INTERNAL_SERVER_ERROR)
        finally:
            duration_ms = (time.time() - start) * 1000
            parsed = urlparse(self.path)
            self.backend.index.db.log_request(
                self.command,
                parsed.path + (("?" + parsed.query) if parsed.query else ""),
                int(getattr(self, "_last_status", 200)),
                duration_ms,
                self.client_address[0] if self.client_address else None,
                error,
            )

    def _handle(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        if path == "/":
            self._html(ADMIN_HTML)
            return
        if path == "/favicon.svg":
            self._svg(FAVICON_SVG)
            return
        if path == "/health":
            self._json({"ok": True, "items": self.backend.index.db.counts_by_kind(), "indexing": self.backend.index.status()})
            return
        if path == "/api/server/status":
            self._json(self.backend.status_json())
            return
        if path == "/api/index/status":
            self._json({"ok": True, "index": self.backend.index.status()})
            return
        if path == "/api/server/requests":
            limit = max(1, min(500, int(_first(query, "limit", "80") or "80")))
            self._json({"ok": True, "items": self.backend.index.db.recent_requests(limit)})
            return
        if path == "/api/server/recent":
            limit = max(1, min(100, int(_first(query, "limit", "30") or "30")))
            self._json({"ok": True, "items": self.backend.index.db.recent_items(limit)})
            return
        if path == "/api/server/config" and self.command == "GET":
            self._json(
                {
                    "ok": True,
                    "path": str(self.backend.config_path),
                    "parsed": load_raw_config(self.backend.config_path),
                    "text": self.backend.config_path.read_text(encoding="utf-8"),
                }
            )
            return
        if path == "/api/server/config" and self.command == "POST":
            body = self._read_json_body()
            text = str(body.get("text", ""))
            if not text.strip():
                self._json({"ok": False, "error": "config text is empty"}, HTTPStatus.BAD_REQUEST)
                return
            old_host = self.backend.config.host
            old_port = self.backend.config.port
            self.backend.config_path.write_text(text, encoding="utf-8")
            self.backend.reload_config()
            self.backend.index.start_background_scan()
            restart_required = self.backend.config.host != old_host or self.backend.config.port != old_port
            self._json({"ok": True, "restart_required": restart_required, "config": self.backend.config_summary()})
            return
        if path == "/api/server/config/reload" and self.command == "POST":
            self.backend.reload_config()
            self.backend.index.start_background_scan()
            self._json({"ok": True, "config": self.backend.config_summary()})
            return
        if path == "/api/server/audio-filters":
            if self.command == "POST":
                filters = self.backend.update_audio_filters(self._read_json_body())
                self._json({"ok": True, "filters": filters, "applies_on_next_play": True})
            else:
                self._json({"ok": True, "filters": self.backend.audio_filters_json()})
            return
        if path == "/api/device/status":
            status, payload, content_type = self.backend.device.request("/api/info")
            self._binary(payload, content_type, HTTPStatus(status))
            return
        if path == "/api/device/app":
            app = _first(query, "set", "home") or "home"
            if app not in {"audio", "audio_stream", "sync", "truevideo", "stream", "graphics", "graphics_player"}:
                self.backend.playback.stop(notify_device=False)
            response = self.backend.device.switch(app)
            self._json({"ok": response.ok, "device": response.__dict__}, HTTPStatus.OK if response.ok else HTTPStatus.SERVICE_UNAVAILABLE)
            return
        if path == "/api/device/wallpaper":
            if self.command == "GET":
                status, payload, content_type = self.backend.device.request("/api/wallpaper")
                self._binary(payload, content_type, HTTPStatus(status))
                return
            if self.command == "DELETE":
                status, payload, content_type = self.backend.device.request("/api/wallpaper", method="DELETE")
                self._binary(payload, content_type, HTTPStatus(status))
                return
            if self.command == "POST":
                body = self._read_json_body()
                if "enabled" in body and "content_base64" not in body:
                    enabled = bool(body.get("enabled"))
                    status, payload, content_type = self.backend.device.request(
                        f"/api/wallpaper/enabled?{urlencode({'set': '1' if enabled else '0'})}",
                        method="POST",
                        data=b"",
                    )
                    self._binary(payload, content_type, HTTPStatus(status))
                    return
                encoded = str(body.get("content_base64", ""))
                try:
                    image = base64.b64decode(encoded, validate=True)
                except (ValueError, binascii.Error):
                    self._json({"ok": False, "error": "invalid base64 wallpaper"}, HTTPStatus.BAD_REQUEST)
                    return
                if len(image) < 4 or len(image) > 96 * 1024 or not image.startswith(b"\xff\xd8"):
                    self._json({"ok": False, "error": "wallpaper must be a JPEG no larger than 96 KB"}, HTTPStatus.BAD_REQUEST)
                    return
                boundary = f"NexusWallpaper{int(time.time() * 1000)}"
                multipart = (
                    f"--{boundary}\r\n"
                    'Content-Disposition: form-data; name="wallpaper"; filename="wallpaper.jpg"\r\n'
                    "Content-Type: image/jpeg\r\n\r\n"
                ).encode("ascii") + image + f"\r\n--{boundary}--\r\n".encode("ascii")
                status, payload, content_type = self.backend.device.request(
                    "/api/wallpaper",
                    method="POST",
                    data=multipart,
                    content_type=f"multipart/form-data; boundary={boundary}",
                    timeout=12,
                )
                self._binary(payload, content_type, HTTPStatus(status))
                return
        if path == "/api/device/littlefs":
            status, payload, content_type = self.backend.device.request("/api/littlefs")
            self._binary(payload, content_type, HTTPStatus(status))
            return
        if path == "/api/device/littlefs/file":
            device_path = _first(query, "path", "") or ""
            encoded_path = urlencode({"path": device_path})
            if self.command == "GET":
                status, payload, content_type = self.backend.device.request(f"/api/littlefs/file?{encoded_path}")
                self._binary(payload, content_type, HTTPStatus(status))
                return
            if self.command == "POST":
                body = self._read_json_body()
                device_path = str(body.get("path", ""))
                content = str(body.get("content", "")).encode("utf-8")
                encoded_path = urlencode({"path": device_path})
                status, payload, content_type = self.backend.device.request(
                    f"/api/littlefs/file?{encoded_path}", method="POST", data=content, content_type="text/plain; charset=utf-8"
                )
                self._binary(payload, content_type, HTTPStatus(status))
                return
            if self.command == "DELETE":
                encoded_path = urlencode({"path": device_path, "confirm": "1"})
                status, payload, content_type = self.backend.device.request(
                    f"/api/littlefs/file?{encoded_path}", method="DELETE"
                )
                self._binary(payload, content_type, HTTPStatus(status))
                return
        if path in {"/api/rescan", "/api/library/rescan"}:
            started = self.backend.index.start_background_scan()
            self._json({"ok": True, "started": started, "indexing": self.backend.index.status()})
            return
        try:
            if path.startswith("/api/library/") and path.endswith("/icon"):
                parts = path.split("/")
                if len(parts) >= 6:
                    kind = self._normalize_kind(parts[3])
                    item_id = unquote(parts[4])
                    item = self.backend.index.get(item_id)
                    if kind is None or not item or item.kind != kind:
                        self._json({"ok": False, "error": "item not found"}, HTTPStatus.NOT_FOUND)
                        return
                    selected = self.backend.index.enrich(item)
                    if self.backend.index.is_unplayable(selected):
                        self.backend.index.delete(selected.id)
                        self._json({"ok": False, "error": "item is invalid or unplayable"}, HTTPStatus.UNPROCESSABLE_ENTITY)
                        return
                    icon = self.backend._icon_for_item(
                        selected,
                        max(16, min(_int_query(query, "icon_size", 64), 128)),
                    )
                    self._json({"ok": True, "id": selected.id, "icon": icon.to_json()})
                    return
                self._json({"ok": False, "error": "bad icon path"}, HTTPStatus.NOT_FOUND)
                return
            if path.startswith("/api/library/") and (path.endswith("/thumbnail.jpg") or path.endswith("/thumbnail")):
                parts = path.split("/")
                if len(parts) >= 6:
                    kind = self._normalize_kind(parts[3])
                    item_id = unquote(parts[4])
                    item = self.backend.index.get(item_id)
                    if kind is None or not item or item.kind != kind:
                        self._json({"ok": False, "error": "item not found"}, HTTPStatus.NOT_FOUND)
                        return
                    selected = self.backend.index.enrich(item)
                    size = max(16, min(_int_query(query, "size", 80), 128))
                    jpeg_bytes = self.backend._thumbnail_jpeg_for_item(selected, size=size)
                    if not jpeg_bytes:
                        self._json({"ok": False, "error": "no thumbnail"}, HTTPStatus.NOT_FOUND)
                        return
                    self._binary(jpeg_bytes, "image/jpeg")
                    return
                self._json({"ok": False, "error": "bad thumbnail path"}, HTTPStatus.NOT_FOUND)
                return
            if path.startswith("/api/library/"):
                kind = self._normalize_kind(path.split("/")[-1])
                if kind is None:
                    self._json({"ok": False, "error": "unknown library kind"}, HTTPStatus.NOT_FOUND)
                    return
                page_size = _int_query(query, "page_size", 0) or _int_query(query, "limit", 0) or None
                self._json(
                    self.backend.page_json(
                        kind,
                        int(_first(query, "page", "1") or "1"),
                        include_icons=str(_first(query, "icons", "true")).lower() != "false",
                        enrich=str(_first(query, "enrich", "true")).lower() != "false",
                        icon_size=max(16, min(_int_query(query, "icon_size", 28), 128)),
                        page_size=page_size,
                    )
                )
                return
            if path in {"/api/audio", "/api/videos", "/api/video", "/api/images", "/api/texts", "/api/text"}:
                kind = self._normalize_kind(path.split("/")[-1])
                page_size = _int_query(query, "page_size", 0) or _int_query(query, "limit", 0) or None
                self._json(
                    self.backend.page_json(
                        kind or "audio",
                        int(_first(query, "page", "1") or "1"),
                        include_icons=str(_first(query, "icons", "true")).lower() != "false",
                        enrich=str(_first(query, "enrich", "true")).lower() != "false",
                        icon_size=max(16, min(_int_query(query, "icon_size", 28), 128)),
                        page_size=page_size,
                    )
                )
                return
            if path.startswith("/api/audio/") and path.endswith("/play"):
                item = self._item_from_path(path, "audio")
                if not item:
                    return
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                self._json(self.backend.playback.play_audio(item, _float_query(query, "start", 0), switch_device=switch_device))
                return
            if path.startswith("/api/video/") and path.endswith("/play"):
                item = self._item_from_path(path, "video")
                if not item:
                    return
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                async_request = str(_first(query, "async", "false")).lower() in {"1", "true", "yes", "on"}
                play = self.backend.playback.submit_video if async_request else self.backend.playback.play_video
                play_options: dict[str, Any] = {
                    "audio": _first(query, "audio", "auto"),
                    "aspect": _first(query, "aspect", None),
                    "start_s": _float_query(query, "start", 0),
                    "switch_device": switch_device,
                    "profile": _first(query, "profile", "balanced"),
                    "fps": _float_query(query, "fps", 0) or None,
                    "jpeg_quality": _int_query(query, "jpeg_quality", 0) or None,
                }
                if async_request:
                    play_options["request_id"] = _first(query, "request_id", None)
                self._json(
                    play(item, **play_options),
                    HTTPStatus.ACCEPTED if async_request else HTTPStatus.OK,
                )
                return
            if path.startswith("/api/image/") and path.endswith("/show"):
                item = self._item_from_path(path, "image")
                if not item:
                    return
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                self._json(
                    self.backend.playback.play_image(
                        item,
                        aspect=_first(query, "aspect", None),
                        seconds=_float_query(query, "seconds", 0) or None,
                        mode=_first(query, "mode", "oneshot") or "oneshot",
                        switch_device=switch_device,
                        profile=_first(query, "profile", "balanced"),
                        fps=_float_query(query, "fps", 0) or None,
                        jpeg_quality=_int_query(query, "jpeg_quality", 0) or None,
                    )
                )
                return
            if path.startswith("/api/image/") and path.endswith("/jpeg"):
                item_id = unquote(path.split("/")[3])
                payload = self.backend.image_preview_jpeg(item_id, _first(query, "aspect", "square") or "square")
                if not payload:
                    self._json({"ok": False, "error": "image not found"}, HTTPStatus.NOT_FOUND)
                    return
                self._binary(payload, "image/jpeg")
                return
            if path.startswith("/api/text/"):
                item_id = unquote(path.split("/")[3])
                text = self.backend.index.read_text(item_id)
                if not text:
                    self._json({"ok": False, "error": "text not found"}, HTTPStatus.NOT_FOUND)
                    return
                self._json(text)
                return
            if path == "/api/playback/status":
                self._json(self.backend.playback.status())
                return
            if path == "/api/playback/operation":
                operation = self.backend.playback.operation_status(_int_query(query, "id", 0))
                if operation is None:
                    self._json({"ok": False, "error": "operation not found"}, HTTPStatus.NOT_FOUND)
                else:
                    self._json(operation)
                return
            if path == "/api/playback/stop":
                notify = str(_first(query, "notify", "true")).lower() not in {"0", "false", "no", "off"}
                async_request = str(_first(query, "async", "false")).lower() in {"1", "true", "yes", "on"}
                result = self.backend.playback.submit_stop(notify_device=notify) if async_request else self.backend.playback.stop(notify_device=notify)
                self._json(result, HTTPStatus.ACCEPTED if async_request else HTTPStatus.OK)
                return
            if path == "/api/playback/seek":
                seconds_raw = _first(query, "seconds")
                seconds = float(seconds_raw) if seconds_raw not in {None, ""} else None
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                async_request = str(_first(query, "async", "false")).lower() in {"1", "true", "yes", "on"}
                seek = self.backend.playback.submit_seek if async_request else self.backend.playback.seek
                seek_options: dict[str, Any] = {
                    "seconds": seconds,
                    "direction": _first(query, "direction", "forward"),
                    "switch_device": switch_device,
                }
                if async_request:
                    seek_options["request_id"] = _first(query, "request_id", None)
                self._json(
                    seek(**seek_options),
                    HTTPStatus.ACCEPTED if async_request else HTTPStatus.OK,
                )
                return
            if path == "/api/playback/next":
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                self._json(self.backend.playback.skip("next", switch_device=switch_device))
                return
            if path == "/api/playback/previous":
                switch_device = str(_first(query, "switch", "true")).lower() not in {"0", "false", "no", "off"}
                self._json(self.backend.playback.skip("previous", switch_device=switch_device))
                return
            if path == "/api/camera/list":
                limit = max(1, min(50, int(_first(query, "limit", "20") or "20")))
                self._json({"ok": True, "items": self.backend.camera_items(limit)})
                return
            if path.startswith("/api/camera/photo/"):
                name = unquote(path.split("/")[-1])
                payload = self.backend.camera_preview_jpeg(name)
                if not payload:
                    self._json({"ok": False, "error": "photo not found"}, HTTPStatus.NOT_FOUND)
                    return
                self._binary(payload, "image/jpeg")
                return
            if path == "/api/camera/upload" and self.command == "POST":
                name = re.sub(r"[^A-Za-z0-9_.-]", "_", _first(query, "name", f"capture_{int(time.time())}.jpg") or "capture.jpg")
                if not name.lower().endswith((".jpg", ".jpeg")):
                    name += ".jpg"
                length = int(self.headers.get("Content-Length", "0") or "0")
                if length <= 0 or length > 8 * 1024 * 1024:
                    self._json({"ok": False, "error": "invalid image size"}, HTTPStatus.BAD_REQUEST)
                    return
                payload = self.rfile.read(length)
                target = self.backend.camera_dir / name
                target.write_bytes(payload)
                self._json({"ok": True, "name": target.name, "size_bytes": len(payload)})
                return
            if path == "/api/upload":
                self._json(
                    {
                        "ok": False,
                        "error": "upload pipeline is reserved but not implemented yet",
                        "write_folder": str(self.backend.config.library.write_folder),
                    },
                    HTTPStatus.NOT_IMPLEMENTED,
                )
                return
            self._json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)
        except Exception as exc:
            self._json({"ok": False, "error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)

    @staticmethod
    def _normalize_kind(raw: str) -> str | None:
        value = raw.lower()
        if value in {"audio", "audios"}:
            return "audio"
        if value in {"video", "videos"}:
            return "video"
        if value in {"image", "images"}:
            return "image"
        if value in {"text", "texts", "books"}:
            return "text"
        return None

    def _item_from_path(self, path: str, expected_kind: str) -> MediaItem | None:
        parts = path.split("/")
        if len(parts) < 4:
            self._json({"ok": False, "error": "missing id"}, HTTPStatus.BAD_REQUEST)
            return None
        item_id = unquote(parts[3])
        item = self.backend.index.get(item_id)
        if not item or item.kind != expected_kind:
            self._json({"ok": False, "error": f"{expected_kind} not found"}, HTTPStatus.NOT_FOUND)
            return None
        selected = self.backend.index.enrich(item)
        if self.backend.index.is_unplayable(selected):
            self.backend.index.delete(selected.id)
            self._json(
                {"ok": False, "error": f"{expected_kind} file is invalid or unplayable"},
                HTTPStatus.UNPROCESSABLE_ENTITY,
            )
            return None
        return selected

    def _json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self._last_status = int(status)
        self.send_response(self._last_status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _html(self, payload: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        body = payload.encode("utf-8")
        self._last_status = int(status)
        self.send_response(self._last_status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _svg(self, payload: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        body = payload.encode("utf-8")
        self._last_status = int(status)
        self.send_response(self._last_status)
        self.send_header("Content-Type", "image/svg+xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _binary(self, payload: bytes, content_type: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        self._last_status = int(status)
        self.send_response(self._last_status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(payload)

    def _read_json_body(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        return json.loads(raw.decode("utf-8"))


def make_server(config: AppConfig, *, dry_run: bool = False, config_path: Path = DEFAULT_CONFIG_PATH) -> ThreadingHTTPServer:
    class Handler(NexusRequestHandler):
        pass

    # Bind first. If another Nexus instance owns the port, fail before starting
    # an indexer thread that could otherwise keep a duplicate process alive.
    server = NexusHTTPServer((config.host, config.port), Handler)
    try:
        Handler.backend = NexusBackend(config, dry_run=dry_run, config_path=config_path)
    except Exception:
        server.server_close()
        raise
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the Nexus media backend.")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG_PATH), help="Path to server config.yml")
    parser.add_argument("--dry-run", action="store_true", help="Do not switch or stream to the ESP32")
    parser.add_argument("--host", help="Override configured bind host")
    parser.add_argument("--port", type=int, help="Override configured bind port")
    args = parser.parse_args(argv)

    config = load_config(Path(args.config))
    if args.host:
        config = replace(config, host=args.host)
    if args.port:
        config = replace(config, port=args.port)
    print(f"Starting Nexus backend on http://{config.host}:{config.port}", flush=True)
    server = make_server(config, dry_run=args.dry_run, config_path=Path(args.config))
    print(f"Nexus backend listening on http://{config.host}:{config.port}", flush=True)
    print(f"Indexed folders: {', '.join(str(p) for p in config.library.read_folders) or '(none)'}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping Nexus backend")
    finally:
        server.server_close()
    return 0
