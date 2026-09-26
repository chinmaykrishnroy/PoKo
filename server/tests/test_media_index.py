from __future__ import annotations

import unittest
from dataclasses import replace
from pathlib import Path

from server.nexus_server.config import LibraryConfig, load_config
from server.nexus_server.media_index import MediaIndex
from helpers import workspace_tempdir


def fake_probe(path: Path, ffprobe: str) -> dict:
    if path.suffix.lower() == ".mp3":
        return {
            "format": {"duration": "120.5", "tags": {"title": "Song Title", "artist": "Artist Name"}},
            "streams": [{"codec_type": "audio"}],
        }
    if path.suffix.lower() == ".mkv":
        return {
            "format": {"duration": "60"},
            "streams": [{"codec_type": "video", "width": 1920, "height": 1080}, {"codec_type": "audio"}],
        }
    return {"streams": [{"codec_type": "video", "width": 28, "height": 28}], "format": {}}


class MediaIndexTests(unittest.TestCase):
    def test_scan_and_page_media(self) -> None:
        with workspace_tempdir() as root:
            (root / "song.mp3").write_bytes(b"fake")
            (root / "movie.mkv").write_bytes(b"fake")
            (root / "cover.jpg").write_bytes(b"fake")
            (root / "book.md").write_text("# Heading\nbody", encoding="utf-8")
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, probe_on_scan=True, db_path=root / "nexus.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            index.rescan()

            audio_page = index.page("audio")
            self.assertFalse(audio_page["empty"])
            self.assertEqual(audio_page["items"][0].title, "Song Title")
            self.assertEqual(audio_page["items"][0].artist, "Artist Name")

            video = index.page("video")["items"][0]
            self.assertTrue(video.has_audio)
            self.assertEqual(video.width, 1920)

            text = index.page("text")["items"][0]
            self.assertEqual(text.title, "Heading")
            self.assertIn("body", index.read_text(text.id)["content"])

    def test_empty_folder_returns_empty_page(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "nexus.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            index.rescan()
            page = index.page("audio")
            self.assertTrue(page["empty"])
            self.assertEqual(page["total"], 0)


if __name__ == "__main__":
    unittest.main()
