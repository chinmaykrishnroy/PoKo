from __future__ import annotations

import json
import threading
import unittest
import urllib.error
import urllib.request
from dataclasses import replace
from pathlib import Path
from unittest import mock

from server.nexus_server.config import LibraryConfig, config_to_yaml, load_config
from server.nexus_server.http_api import make_server
from helpers import workspace_tempdir


class HttpApiTests(unittest.TestCase):
    def test_port_is_bound_before_backend_workers_start(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            first = make_server(config, dry_run=True)
            try:
                occupied = replace(config, port=first.server_address[1])
                with mock.patch("server.nexus_server.http_api.NexusBackend") as backend:
                    with self.assertRaises(OSError):
                        make_server(occupied, dry_run=True)
                    backend.assert_not_called()
            finally:
                first.server_close()

    def test_health_and_empty_library(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
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
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
            config = replace(base, host="127.0.0.1", port=0, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            config_path = root / "config.yml"
            config_path.write_text(config_to_yaml(config), encoding="utf-8")
            server = make_server(config, dry_run=True, config_path=config_path)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                base_url = f"http://{host}:{port}"
                with urllib.request.urlopen(base_url + "/", timeout=5) as response:
                    self.assertIn("Nexus Control", response.read().decode("utf-8"))
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
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(base_url + "/api/device/littlefs", timeout=5)
                self.assertEqual(error.exception.code, 503)
                bad_wallpaper = urllib.request.Request(
                    base_url + "/api/device/wallpaper",
                    data=json.dumps({"content_base64": "bm90LWEtanBlZw=="}).encode("utf-8"),
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(bad_wallpaper, timeout=5)
                self.assertEqual(error.exception.code, 400)
            finally:
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    unittest.main()
