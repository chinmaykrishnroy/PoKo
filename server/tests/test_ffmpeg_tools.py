from __future__ import annotations

import unittest
import sys
from dataclasses import replace
from pathlib import Path

from server.poko_server.config import load_config
from server.poko_server.ffmpeg_tools import (
    audio_filter,
    audio_tcp_command,
    graphics_video_tcp_command,
    media_kind_for,
    metadata_from_probe,
    synced_audio_pipe_command,
    synced_video_pipe_command,
    thumbnail_icon,
    video_filter,
    _run_json_command,
)


_config_path = Path(__file__).resolve().parents[1] / "config.yml"
if not _config_path.exists():
    _config_path = Path(__file__).resolve().parents[1] / "config.example.yml"
CONFIG = load_config(_config_path)


class FFmpegCommandTests(unittest.TestCase):
    def test_square_and_fit_filters(self) -> None:
        self.assertIn(f"crop={CONFIG.display.width}:{CONFIG.display.height}", video_filter("square", 12, CONFIG.display.width, CONFIG.display.height))
        self.assertIn(f"pad={CONFIG.display.width}:{CONFIG.display.height}", video_filter("fit", 12, CONFIG.display.width, CONFIG.display.height))
        self.assertIn("crop=240:240", video_filter("square", 12, 240, 240))
        self.assertIn("pad=240:240", video_filter("fit", 12, 240, 240))

    def test_audio_command_targets_audio_port(self) -> None:
        cmd = audio_tcp_command(Path("song.mp3"), CONFIG, 12.5)
        self.assertIn("-ss", cmd)
        self.assertIn("12.500", cmd)
        self.assertIn("libmp3lame", cmd)
        self.assertTrue(cmd[-1].endswith(":1235?tcp_nodelay=1"))

    def test_audio_filter_chain_is_optional(self) -> None:
        self.assertIsNone(audio_filter(CONFIG))
        filtered = replace(
            CONFIG,
            ffmpeg=replace(
                CONFIG.ffmpeg,
                highpass_enabled=True,
                highpass_hz=95,
                lowpass_enabled=True,
                lowpass_hz=12500,
            ),
        )
        self.assertEqual(audio_filter(filtered), "highpass=f=95,lowpass=f=12500")
        self.assertIn("highpass=f=95,lowpass=f=12500", audio_tcp_command(Path("song.mp3"), filtered))

    def test_graphics_command_targets_stream_port(self) -> None:
        cmd = graphics_video_tcp_command(Path("movie.mkv"), CONFIG, aspect="fit")
        self.assertIn("mjpeg", cmd)
        self.assertIn(f"pad={CONFIG.display.width}:{CONFIG.display.height}", " ".join(cmd))
        self.assertTrue(cmd[-1].endswith(":1234?tcp_nodelay=1"))

    def test_motion_jpeg_is_an_image_and_can_loop(self) -> None:
        self.assertEqual(media_kind_for(Path("animation.mjpeg")), "image")
        cmd = graphics_video_tcp_command(Path("animation.mjpeg"), CONFIG, stream_loop=True)
        self.assertIn("-stream_loop", cmd)
        self.assertEqual(cmd[cmd.index("-stream_loop") + 1], "-1")

    def test_synced_commands_use_pipes(self) -> None:
        audio = synced_audio_pipe_command(Path("movie.mkv"), CONFIG)
        video = synced_video_pipe_command(Path("movie.mkv"), CONFIG)
        self.assertEqual(audio[-1], "-")
        self.assertEqual(video[-1], "-")
        self.assertIn("pcm_s16le", audio)
        self.assertIn("image2pipe", video)

    def test_json_command_tolerates_non_console_bytes(self) -> None:
        cmd = [
            sys.executable,
            "-c",
            "import sys; sys.stdout.buffer.write(b'{\"format\":{\"tags\":{\"title\":\"bad\\x8dtag\"}}}')",
        ]
        parsed = _run_json_command(cmd)
        self.assertEqual(parsed["format"]["tags"]["title"], "bad\ufffdtag")

    def test_audio_metadata_uses_stream_tags_and_duration(self) -> None:
        meta = metadata_from_probe(
            Path("song.mp3"),
            "audio",
            {
                "format": {"tags": {"TITLE": "Song"}},
                "streams": [{"codec_type": "audio", "duration": "42.5", "tags": {"ARTISTS": "Singer"}}],
            },
        )
        self.assertEqual(meta["artist"], "Singer")
        self.assertEqual(meta["duration_s"], 42.5)

    def test_video_title_uses_filename_instead_of_audio_track_label(self) -> None:
        meta = metadata_from_probe(
            Path("Ben 10 Omniverse - S04E09.mkv"),
            "video",
            {
                "format": {},
                "streams": [
                    {"codec_type": "video", "width": 1280, "height": 720},
                    {"codec_type": "audio", "tags": {"title": "Stereo"}},
                ],
            },
        )
        self.assertEqual(meta["title"], "Ben 10 Omniverse - S04E09")

    def test_thumbnail_icon_prefers_jpeg_payload(self) -> None:
        icon = thumbnail_icon(Path("missing.mp3"), "audio", CONFIG)
        self.assertIn(icon.mime, {"image/jpeg", "image/svg+xml"})


if __name__ == "__main__":
    unittest.main()

