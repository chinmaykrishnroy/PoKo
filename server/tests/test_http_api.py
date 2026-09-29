from __future__ import annotations

import json
import threading
import unittest
import urllib.error
import urllib.request
from dataclasses import replace
from pathlib import Path
from unittest import mock

from server.poko_server.config import DisplayConfig, LibraryConfig, config_to_yaml, load_config
from server.poko_server.http_api import make_server
from server.poko_server.models import MediaItem
from helpers import workspace_tempdir


_config_path = Path(__file__).resolve().parents[1] / "config.yml"
if not _config_path.exists():
    _config_path = Path(__file__).resolve().parents[1] / "config.example.yml"


class HttpApiTests(unittest.TestCase):
    def test_port_is_bound_before_backend_workers_start(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            first = make_server(config, dry_run=True)
            try:
                occupied = replace(config, port=first.server_address[1])
                with mock.patch("server.poko_server.http_api.PokoBackend") as backend:
                    with self.assertRaises(OSError):
                        make_server(occupied, dry_run=True)
                    backend.assert_not_called()
            finally:
                first.server_close()

    def test_health_and_empty_library(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            server = make_server(config, dry_run=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                with urllib.request.urlopen(f"http://{host}:{port}/health", timeout=5) as response:
                    health = json.loads(response.read().decode("utf-8"))
                self.assertTrue(health["ok"])

                with urllib.request.urlopen(f"http://{host}:{port}/api/library/audio?page=1", timeout=5) as response:
                    page = json.loads(response.read().decode("utf-8"))
                self.assertTrue(page["empty"])
                self.assertEqual(page["page_size"], 5)
            finally:
                server.shutdown()
                server.server_close()

    def test_admin_filters_and_dry_run_device_proxy(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(
                base,
                host="127.0.0.1",
                port=0,
                display=DisplayConfig(128, 128),
                library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"),
            )
            config_path = root / "config.yml"
            config_path.write_text(config_to_yaml(config), encoding="utf-8")
            server = make_server(config, dry_run=True, config_path=config_path)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                base_url = f"http://{host}:{port}"
                with urllib.request.urlopen(base_url + "/", timeout=5) as response:
                    html = response.read().decode("utf-8")
                    self.assertIn("PoKo Control", html)
                    self.assertNotIn("Nexus", html)
                    self.assertNotIn("240 × 240", html)
                payload = json.dumps(
                    {"highpass_enabled": True, "highpass_hz": 90, "lowpass_enabled": True, "lowpass_hz": 14000}
                ).encode("utf-8")
                request = urllib.request.Request(
                    base_url + "/api/server/audio-filters",
                    data=payload,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=5) as response:
                    filters = json.loads(response.read().decode("utf-8"))["filters"]
                self.assertEqual(filters["highpass"]["cutoff_hz"], 90)
                self.assertEqual(filters["lowpass"]["cutoff_hz"], 14000)
                persisted = load_config(config_path)
                self.assertEqual((persisted.display.width, persisted.display.height), (128, 128))
                with mock.patch.object(server.RequestHandlerClass.backend.device, "request", return_value=(200, b'{"app_state":6,"battery_pct":84}', "application/json")) as request_device:
                    with urllib.request.urlopen(base_url + "/api/device/status", timeout=5) as response:
                        self.assertEqual(json.load(response)["app_state"], 6)
                    request_device.assert_called_with("/api/health")
                    with urllib.request.urlopen(base_url + "/api/device/power?dim_timeout=30&wifi_sleep=0", timeout=5) as response:
                        self.assertEqual(response.status, 200)
                    request_device.assert_called_with("/api/power?dim_timeout=30&wifi_sleep=0")
                    with urllib.request.urlopen(base_url + "/api/device/sys?brightness=75", timeout=5) as response:
                        self.assertEqual(response.status, 200)
                    request_device.assert_called_with("/api/sys?brightness=75")
                    with urllib.request.urlopen(base_url + "/api/device/gallery/files", timeout=5) as response:
                        self.assertEqual(response.status, 200)
                    request_device.assert_called_with("/api/gallery/files")
                for legacy_path in ("/api/device/littlefs", "/api/device/wallpaper", "/api/device/littlefs/file"):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(base_url + legacy_path, timeout=5)
                    self.assertEqual(error.exception.code, 404)
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(base_url + "/api/device/app?set=stream", timeout=5)
                self.assertEqual(error.exception.code, 400)
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(base_url + "/api/device/sys?brightness=evil", timeout=5)
                self.assertEqual(error.exception.code, 400)
            finally:
                server.shutdown()
                server.server_close()

    def test_audio_playback_failure_is_not_an_http_success(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "catalog.db"))
            server = make_server(config, dry_run=True)
            server.RequestHandlerClass.backend.index.join(timeout=2)
            path = root / "song.mp3"
            path.write_bytes(b"audio")
            item = MediaItem(id="song", kind="audio", path=path, title="Song", extension=".mp3", size_bytes=5, duration_s=10, artist="Artist")
            server.RequestHandlerClass.backend.index.db.upsert_item(item, "test", enriched=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with mock.patch.object(server.RequestHandlerClass.backend.playback, "play_audio", return_value={"ok": False, "error": "FFmpeg failed"}):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(f"http://127.0.0.1:{server.server_address[1]}/api/audio/song/play", timeout=5)
                    self.assertEqual(error.exception.code, 502)
                    self.assertFalse(json.loads(error.exception.read())["ok"])
            finally:
                server.shutdown()
                server.server_close()

    def test_single_item_page_refills_after_invalid_media_is_removed(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "catalog.db"))
            server = make_server(config, dry_run=True)
            backend = server.RequestHandlerClass.backend
            backend.index.join(timeout=2)
            (root / "bad.mp3").write_bytes(b"invalid")
            (root / "good.mp3").write_bytes(b"valid")
            backend.index._probe_fn = lambda path, ffprobe: {} if path.name == "bad.mp3" else {
                "format": {"duration": "60", "tags": {"artist": "Artist"}},
                "streams": [{"codec_type": "audio"}],
            }
            backend.index.rescan()
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{server.server_address[1]}/api/library/audio?page=1&page_size=1&icons=false", timeout=5) as response:
                    page = json.load(response)
                self.assertEqual(page["total"], 1)
                self.assertEqual([item["title"] for item in page["items"]], ["good"])
            finally:
                server.shutdown()
                server.server_close()

    def test_camera_jpeg_listing_validation_and_bad_query_status(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            config_path = root / "config.yml"
            config_path.write_text(config_to_yaml(config), encoding="utf-8")
            server = make_server(config, dry_run=True, config_path=config_path)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                base_url = f"http://{host}:{port}"
                good = urllib.request.Request(
                    base_url + "/api/camera/upload?name=capture.jpeg",
                    data=b"\xff\xd8\xff\xd9",
                    headers={"Content-Type": "image/jpeg"},
                    method="POST",
                )
                with urllib.request.urlopen(good, timeout=5) as response:
                    self.assertTrue(json.loads(response.read().decode("utf-8"))["ok"])
                with urllib.request.urlopen(base_url + "/api/camera/list", timeout=5) as response:
                    listed = json.loads(response.read().decode("utf-8"))["items"]
                self.assertEqual([item["name"] for item in listed], ["capture.jpeg"])

                bad = urllib.request.Request(
                    base_url + "/api/camera/upload?name=bad.jpg",
                    data=b"not a jpeg",
                    headers={"Content-Type": "image/jpeg"},
                    method="POST",
                )
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(bad, timeout=5)
                self.assertEqual(error.exception.code, 400)

                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(base_url + "/api/library/audio?page=banana", timeout=5)
                self.assertEqual(error.exception.code, 400)
            finally:
                server.shutdown()
                server.server_close()

    def test_config_reload_moves_index_to_new_database(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "one.db"))
            config_path = root / "config.yml"
            config_path.write_text(config_to_yaml(config), encoding="utf-8")
            server = make_server(config, dry_run=True, config_path=config_path)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                base_url = f"http://{host}:{port}"
                updated = replace(config, library=replace(config.library, db_path=root / "two.db"))
                payload = json.dumps({"text": config_to_yaml(updated)}).encode("utf-8")
                request = urllib.request.Request(
                    base_url + "/api/server/config",
                    data=payload,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=5) as response:
                    self.assertTrue(json.loads(response.read().decode("utf-8"))["ok"])
                with urllib.request.urlopen(base_url + "/api/index/status", timeout=5) as response:
                    status = json.loads(response.read().decode("utf-8"))["index"]
                self.assertEqual(Path(status["db_path"]), root / "two.db")
            finally:
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    unittest.main()

