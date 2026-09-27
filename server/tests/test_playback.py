from __future__ import annotations

import unittest
import time
from dataclasses import replace
from pathlib import Path
from unittest import mock

from server.poko_server.config import LibraryConfig, load_config
from server.poko_server.device import DeviceClient, DeviceResponse
from server.poko_server.media_index import MediaIndex
from server.poko_server.models import MediaItem
from server.poko_server.playback import PlaybackManager, SyncedAVStreamer, resolve_video_tuning
from helpers import workspace_tempdir


class FakeDevice(DeviceClient):
    def __init__(self, config):
        super().__init__(config, enabled=False)
        self.apps: list[str] = []

    def switch(self, app: str) -> DeviceResponse:
        self.apps.append(app)
        return DeviceResponse(ok=True, app=app, body="fake")


class PlaybackTests(unittest.TestCase):
    def test_synced_socket_becomes_blocking_after_connect(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.timeout = 0.4

            def settimeout(self, value) -> None:
                self.timeout = value

            def setsockopt(self, *args) -> None:
                pass

        config = load_config(Path(__file__).resolve().parents[1] / "config.yml")
        item = MediaItem(id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1)
        streamer = SyncedAVStreamer(item, config)
        fake_socket = FakeSocket()
        with mock.patch("server.poko_server.playback.socket.create_connection", return_value=fake_socket):
            connected = streamer._connect(config.nexus.video_audio_port)
        self.assertIs(connected, fake_socket)
        self.assertIsNone(fake_socket.timeout)

    def test_video_quality_profiles_are_bounded(self) -> None:
        config = load_config(Path(__file__).resolve().parents[1] / "config.yml")
        profile, fps, quality = resolve_video_tuning(config, synced=True, profile="quality", fps=99, jpeg_quality=1)
        self.assertEqual(profile, "quality")
        self.assertEqual(fps, 20)
        self.assertEqual(quality, 4)

    def _manager(self):
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            index = MediaIndex(config, probe_fn=lambda path, ffprobe: {})
            device = FakeDevice(config)
            manager = PlaybackManager(config, index, device, dry_run=True)
            yield manager, device

    def test_video_with_audio_uses_video_player(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1",
                kind="video",
                path=Path("movie.mkv"),
                title="Movie",
                extension=".mkv",
                size_bytes=1,
                has_audio=True,
            )
            result = manager.play_video(item, audio="true")
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps[-1], "sync")
            self.assertEqual(result["playback"]["mode"], "video_player")

    def test_video_without_audio_uses_graphics_player(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1",
                kind="video",
                path=Path("movie.mkv"),
                title="Movie",
                extension=".mkv",
                size_bytes=1,
                has_audio=False,
            )
            result = manager.play_video(item, audio="auto")
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps[-1], "stream")
            self.assertEqual(result["playback"]["mode"], "graphics_player")

    def test_video_can_skip_device_switch_for_embedded_ui(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1",
                kind="video",
                path=Path("movie.mkv"),
                title="Movie",
                extension=".mkv",
                size_bytes=1,
                has_audio=True,
            )
            result = manager.play_video(item, audio="true", switch_device=False)
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps, [])
            self.assertEqual(result["device"]["body"], "not-switched")
            self.assertEqual(result["playback"]["mode"], "video_player")

    def test_async_video_operation_reports_completion(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1",
                kind="video",
                path=Path("movie.mkv"),
                title="Movie",
                extension=".mkv",
                size_bytes=1,
                has_audio=True,
            )
            accepted = manager.submit_video(item, audio="true", switch_device=False)
            self.assertTrue(accepted["accepted"])
            self.assertEqual(accepted["mode"], "video_player")

            deadline = time.monotonic() + 2
            operation = manager.operation_status(accepted["operation_id"])
            while operation and operation["status"] not in {"succeeded", "failed"} and time.monotonic() < deadline:
                time.sleep(0.01)
                operation = manager.operation_status(accepted["operation_id"])

            self.assertIsNotNone(operation)
            self.assertEqual(operation["status"], "succeeded")
            self.assertTrue(operation["result"]["ok"])
            self.assertEqual(device.apps, [])

    def test_async_video_request_id_is_idempotent(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1",
                kind="video",
                path=Path("movie.mkv"),
                title="Movie",
                extension=".mkv",
                size_bytes=1,
                has_audio=True,
            )
            first = manager.submit_video(
                item,
                audio="true",
                switch_device=False,
                request_id="nexus-v1-launch-123",
            )
            retry = manager.submit_video(
                item,
                audio="true",
                switch_device=False,
                request_id="nexus-v1-launch-123",
            )

            self.assertEqual(retry["operation_id"], first["operation_id"])
            self.assertTrue(retry["duplicate"])
            self.assertEqual(retry["request_id"], "nexus-v1-launch-123")

    def test_audio_uses_audio_player(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(id="a1", kind="audio", path=Path("song.mp3"), title="Song", extension=".mp3", size_bytes=1)
            result = manager.play_audio(item)
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps[-1], "audio")
            self.assertEqual(result["playback"]["mode"], "audio_player")

    def test_audio_can_skip_device_switch_for_embedded_ui(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(id="a1", kind="audio", path=Path("song.mp3"), title="Song", extension=".mp3", size_bytes=1)
            result = manager.play_audio(item, switch_device=False)
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps, [])
            self.assertEqual(result["device"]["body"], "not-switched")

    def test_audio_seek_can_skip_device_switch_for_embedded_ui(self) -> None:
        for manager, device in self._manager():
            path = manager.config.library.write_folder / "song.mp3"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"audio")
            item = MediaItem(id="a1", kind="audio", path=path, title="Song", extension=".mp3", size_bytes=5)
            manager.index.db.upsert_item(item, "test", enriched=True)

            manager.play_audio(item, switch_device=False)
            result = manager.seek(seconds=5, switch_device=False)

            self.assertTrue(result["ok"])
            self.assertEqual(device.apps, [])
            self.assertEqual(result["device"]["body"], "not-switched")

    def test_video_seek_can_skip_device_switch_for_embedded_ui(self) -> None:
        for manager, device in self._manager():
            path = manager.config.library.write_folder / "movie.mkv"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"video")
            item = MediaItem(id="v1", kind="video", path=path, title="Movie", extension=".mkv", size_bytes=5, has_audio=True)
            manager.index.db.upsert_item(item, "test", enriched=True)

            manager.play_video(item, audio="true", switch_device=False)
            result = manager.seek(seconds=5, switch_device=False)

            self.assertTrue(result["ok"])
            self.assertEqual(device.apps, [])
            self.assertEqual(result["device"]["body"], "not-switched")

    def test_motion_image_modes_preserve_embedded_gallery(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="i1",
                kind="image",
                path=Path("animation.gif"),
                title="Animation",
                extension=".gif",
                size_bytes=1,
                duration_s=4.5,
                metadata={"animated": True},
            )
            result = manager.play_image(item, mode="oneshot", switch_device=False, profile="quality")
            self.assertTrue(result["ok"])
            self.assertEqual(device.apps, [])
            self.assertEqual(result["playback"]["counters"]["motion_mode"], "oneshot")
            self.assertEqual(result["playback"]["counters"]["motion_seconds"], 4.5)
            self.assertEqual(result["playback"]["counters"]["profile"], "quality")
            self.assertNotIn("-stream_loop", result["playback"]["command"])

            looped = manager.play_image(item, mode="loop", switch_device=False)
            self.assertIn("-stream_loop", looped["playback"]["command"])


if __name__ == "__main__":
    unittest.main()
