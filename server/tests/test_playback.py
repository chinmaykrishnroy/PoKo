from __future__ import annotations

import unittest
import time
import socket
import threading
from dataclasses import replace
from pathlib import Path
from unittest import mock

from server.poko_server.config import LibraryConfig, load_config
from server.poko_server.device import DeviceClient, DeviceResponse
from server.poko_server.media_index import MediaIndex
from server.poko_server.models import MediaItem
from server.poko_server.playback import PlaybackManager, SyncedAVStreamer, resolve_video_tuning
from helpers import workspace_tempdir


_config_path = Path(__file__).resolve().parents[1] / "config.yml"
if not _config_path.exists():
    _config_path = Path(__file__).resolve().parents[1] / "config.example.yml"


class FakeDevice(DeviceClient):
    def __init__(self, config):
        super().__init__(config, enabled=False)
        self.apps: list[str] = []

    def switch(self, app: str) -> DeviceResponse:
        self.apps.append(app)
        return DeviceResponse(ok=True, app=app, body="fake")


class FailingDevice(FakeDevice):
    def switch(self, app: str) -> DeviceResponse:
        self.apps.append(app)
        return DeviceResponse(ok=False, app=app, error="device offline")


class PlaybackTests(unittest.TestCase):
    def test_embedded_silent_video_keeps_nav1_transport(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(id="silent", kind="video", path=Path("silent.mp4"), title="Silent", extension=".mp4", size_bytes=1, has_audio=False)
            result = manager.play_video(item, audio="true", switch_device=False)
            self.assertEqual(result["playback"]["mode"], "video_player")
            self.assertFalse(result["playback"]["with_audio"])
            self.assertEqual(device.apps, [])

    def test_silent_nav1_start_does_not_wait_for_audio(self) -> None:
        item = MediaItem(id="silent", kind="video", path=Path("silent.mp4"), title="Silent", extension=".mp4", size_bytes=1, has_audio=False)
        streamer = SyncedAVStreamer(item, load_config(_config_path))

        def video_sender():
            with streamer.lock:
                streamer.counters["video_connected"] = True
            streamer.video_ready.set()
            streamer.release_senders.wait(1)

        with mock.patch.object(streamer, "_audio_sender") as audio, mock.patch.object(streamer, "_video_sender", side_effect=video_sender):
            try:
                self.assertTrue(streamer.start(timeout_s=0.2))
                audio.assert_not_called()
                self.assertEqual(len(streamer.commands), 1)
            finally:
                streamer.stop()

    def test_stop_unblocks_sender_before_next_video(self) -> None:
        config = load_config(_config_path)
        item = MediaItem(id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1)
        streamer = SyncedAVStreamer(item, config)
        entered = threading.Event()
        released = threading.Event()

        class StalledSocket:
            def settimeout(self, value): pass
            def setsockopt(self, *args): pass
            def sendall(self, payload):
                entered.set()
                released.wait(5)
            def shutdown(self, how): released.set()
            def close(self): released.set()

        sender = StalledSocket()
        try:
            with mock.patch("server.poko_server.playback.socket.create_connection", return_value=sender):
                streamer._connect(config.poko.video_frames_port)
            thread = threading.Thread(target=streamer._send_packet, args=(sender, 2, 0, b"frame"), daemon=True)
            streamer.threads = [thread]
            thread.start()
            self.assertTrue(entered.wait(1))
            streamer.stop()
            self.assertFalse(thread.is_alive(), "previous video sender survived stop and can hold the old stream open")
        finally:
            released.set()
            for thread in streamer.threads:
                thread.join(timeout=2)

    def test_synced_socket_becomes_blocking_after_connect(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.timeout = 0.4

            def settimeout(self, value) -> None:
                self.timeout = value

            def setsockopt(self, *args) -> None:
                pass

        config = load_config(_config_path)
        item = MediaItem(id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1)
        streamer = SyncedAVStreamer(item, config)
        fake_socket = FakeSocket()
        with mock.patch("server.poko_server.playback.socket.create_connection", return_value=fake_socket):
            connected = streamer._connect(config.poko.video_audio_port)
        self.assertIs(connected, fake_socket)
        self.assertIsNone(fake_socket.timeout)

    def test_synced_stream_targets_observed_device_ip(self) -> None:
        class FakeSocket:
            def settimeout(self, value): pass
            def setsockopt(self, *args): pass

        config = load_config(_config_path)
        item = MediaItem(id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1)
        streamer = SyncedAVStreamer(item, config, target_host="192.168.0.4")
        with mock.patch("server.poko_server.playback.socket.create_connection", return_value=FakeSocket()) as connect:
            streamer._connect(config.poko.video_audio_port)
        connect.assert_called_once_with(("192.168.0.4", config.poko.video_audio_port), timeout=1.0)

    def test_exited_ffmpeg_is_not_reported_as_playing(self) -> None:
        for manager, _ in self._manager():
            item = MediaItem(id="a1", kind="audio", path=Path("song.mp3"), title="Song", extension=".mp3", size_bytes=1)
            manager.play_audio(item, switch_device=False)
            manager._process.process = mock.Mock()
            manager._process.process.poll.return_value = 1
            self.assertFalse(manager.status()["active"])
            self.assertEqual(manager.status()["mode"], "idle")
            manager.close()

    def test_new_async_command_cancels_queued_playback(self) -> None:
        for manager, _ in self._manager():
            entered = threading.Event()
            release = threading.Event()
            executed = []
            def slow():
                entered.set()
                release.wait(2)
                return {"ok": True}
            first = manager._submit_operation("play_video", slow)
            self.assertTrue(entered.wait(1))
            old = manager._submit_operation("play_video", lambda: executed.append("old") or {"ok": True})
            latest = manager._submit_operation("stop", lambda: executed.append("stop") or {"ok": True})
            release.set()
            deadline = time.monotonic() + 2
            while manager.operation_status(latest["operation_id"])["status"] == "queued" and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(executed, ["stop"])
            self.assertEqual(manager.operation_status(old["operation_id"])["status"], "cancelled")
            manager.close()

    def test_close_cancels_queued_commands_and_rejects_new_ones(self) -> None:
        for manager, _ in self._manager():
            entered = threading.Event()
            release = threading.Event()
            executed = []
            def slow():
                entered.set()
                release.wait(1)
                return {"ok": True}
            manager._submit_operation("seek", slow)
            self.assertTrue(entered.wait(1))
            pending = manager._submit_operation("stop", lambda: executed.append("late") or {"ok": True})
            release.set()
            manager.close()
            self.assertFalse(manager._operation_worker.is_alive())
            self.assertEqual(executed, [])
            self.assertEqual(manager.operation_status(pending["operation_id"])["status"], "cancelled")
            self.assertFalse(manager.submit_stop()["ok"])

    def test_device_switch_requires_json_acknowledgement(self) -> None:
        for manager, _ in self._manager():
            client = DeviceClient(manager.config)
            class Response:
                def __enter__(self): return self
                def __exit__(self, *args): pass
                def read(self): return b'{"ok":false,"app":4}'
            with mock.patch("server.poko_server.device.urllib.request.urlopen", return_value=Response()):
                self.assertFalse(client.switch("sync").ok)
            manager.close()

    def test_video_quality_profiles_are_bounded(self) -> None:
        config = load_config(_config_path)
        profile, fps, quality = resolve_video_tuning(config, synced=True, profile="quality", fps=99, jpeg_quality=1)
        self.assertEqual(profile, "quality")
        self.assertEqual(fps, 20)
        self.assertEqual(quality, 4)

    def _manager(self):
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "poko.db"))
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

    def test_video_without_audio_uses_nav1_listener(self) -> None:
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
            self.assertEqual(device.apps[-1], "sync")
            self.assertEqual(result["playback"]["mode"], "video_player")
            self.assertFalse(result["playback"]["with_audio"])

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
                request_id="poko-v1-launch-123",
            )
            retry = manager.submit_video(
                item,
                audio="true",
                switch_device=False,
                request_id="poko-v1-launch-123",
            )

            self.assertEqual(retry["operation_id"], first["operation_id"])
            self.assertTrue(retry["duplicate"])
            self.assertEqual(retry["request_id"], "poko-v1-launch-123")

    def test_remote_image_playback_reports_unsupported_transport(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(id="i1", kind="image", path=Path("cover.jpg"), title="Cover", extension=".jpg", size_bytes=1)
            result = manager.play_image(item)
            self.assertFalse(result["ok"])
            self.assertIn("not supported", result["error"])
            self.assertEqual(device.apps, [])
            manager.close()

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

    def test_failed_device_switch_does_not_start_playback(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(_config_path)
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "poko.db"))
            index = MediaIndex(config, probe_fn=lambda path, ffprobe: {})
            device = FailingDevice(config)
            manager = PlaybackManager(config, index, device, dry_run=True)
            try:
                audio = MediaItem(id="a1", kind="audio", path=Path("song.mp3"), title="Song", extension=".mp3", size_bytes=1)
                video = MediaItem(id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1, has_audio=True)
                image = MediaItem(id="i1", kind="image", path=Path("cover.jpg"), title="Cover", extension=".jpg", size_bytes=1)
                for call in (
                    lambda: manager.play_audio(audio),
                    lambda: manager.play_video(video, audio=True),
                    lambda: manager.play_image(image),
                ):
                    result = call()
                    self.assertFalse(result["ok"])
                    self.assertFalse(result.get("playback", {"active": False})["active"])
                    self.assertIsNone(manager._process)
                    self.assertIsNone(manager._streamer)
            finally:
                manager.close()

    def test_audio_auto_is_case_insensitive_and_negative_start_is_clamped(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(
                id="v1", kind="video", path=Path("movie.mkv"), title="Movie", extension=".mkv", size_bytes=1, has_audio=True
            )
            result = manager.play_video(item, audio="AUTO", start_s=-12, switch_device=False)
            self.assertTrue(result["ok"])
            self.assertEqual(result["playback"]["mode"], "video_player")
            self.assertEqual(result["playback"]["start_s"], 0)
            manager.close()

    def test_non_positive_image_duration_uses_safe_default(self) -> None:
        for manager, device in self._manager():
            item = MediaItem(id="i1", kind="image", path=Path("still.jpg"), title="Still", extension=".jpg", size_bytes=1)
            result = manager.play_image(item, seconds=0, switch_device=False)
            self.assertEqual(result["playback"]["counters"]["hold_seconds"], manager.config.defaults.image_hold_seconds)
            self.assertIn("-t", result["playback"]["command"])
            manager.close()

    def test_ffmpeg_spawn_failure_is_returned_as_playback_error(self) -> None:
        for manager, device in self._manager():
            audio = MediaItem(id="a1", kind="audio", path=Path("song.mp3"), title="Song", extension=".mp3", size_bytes=1)
            image = MediaItem(id="i1", kind="image", path=Path("still.jpg"), title="Still", extension=".jpg", size_bytes=1)
            with mock.patch("server.poko_server.playback.ProcessHandle", side_effect=FileNotFoundError("ffmpeg missing")):
                for call in (
                    lambda: manager.play_audio(audio, switch_device=False),
                    lambda: manager.play_image(image, switch_device=False),
                ):
                    result = call()
                    self.assertFalse(result["ok"])
                    self.assertIn("failed to start FFmpeg", result["error"])
                    self.assertFalse(result["playback"]["active"])
            manager.close()


if __name__ == "__main__":
    unittest.main()

