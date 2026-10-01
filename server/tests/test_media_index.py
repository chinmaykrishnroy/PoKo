from __future__ import annotations

import hashlib
import os
import unittest
from unittest import mock
from dataclasses import replace
from pathlib import Path

from server.poko_server.config import LibraryConfig, load_config
from server.poko_server.media_index import MediaIndex
from server.poko_server.models import MediaItem
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
            cfg_path = Path(__file__).resolve().parents[1] / "config.yml"
            if not cfg_path.exists():
                cfg_path = Path(__file__).resolve().parents[1] / "config.example.yml"
            base = load_config(cfg_path)
            config = replace(base, library=LibraryConfig([root], root / "write", 5, probe_on_scan=True, db_path=root / "poko.db"))
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
            cfg_path = Path(__file__).resolve().parents[1] / "config.yml"
            if not cfg_path.exists():
                cfg_path = Path(__file__).resolve().parents[1] / "config.example.yml"
            base = load_config(cfg_path)
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "poko.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            index.rescan()
            page = index.page("audio")
            self.assertTrue(page["empty"])
            self.assertEqual(page["total"], 0)

    def test_changed_media_invalidates_enrichment(self) -> None:
        with workspace_tempdir() as root:
            path = root / "movie.mkv"
            path.write_bytes(b"old")
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "catalog.db"))
            probes = []
            def probe(media, ffprobe):
                probes.append(media)
                return {"format": {"duration": "9"}, "streams": [{"codec_type": "video", "width": 128, "height": 128}]}
            index = MediaIndex(config, probe_fn=probe)
            index.rescan()
            original = index.enrich(index.page("video")["items"][0])
            self.assertEqual(original.duration_s, 9)
            path.write_bytes(b"new content")
            index.rescan()
            changed = index.page("video")["items"][0]
            self.assertIsNone(changed.duration_s)
            self.assertFalse(changed.has_audio)
            self.assertEqual(index.enrich(changed).duration_s, 9)
            self.assertEqual(len(probes), 2)

    def test_unreadable_image_is_removed_from_catalog(self) -> None:
        with workspace_tempdir() as root:
            (root / "bad.jpg").write_bytes(b"not a JPEG")
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "catalog.db"))
            index = MediaIndex(config, probe_fn=lambda path, ffprobe: {})
            index.rescan()
            item = index.enrich(index.page("image")["items"][0])
            self.assertTrue(index.is_unplayable(item))

    def test_stop_and_join(self) -> None:
        with workspace_tempdir() as root:
            cfg_path = Path(__file__).resolve().parents[1] / "config.yml"
            if not cfg_path.exists():
                cfg_path = Path(__file__).resolve().parents[1] / "config.example.yml"
            base = load_config(cfg_path)
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "poko.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            self.assertTrue(index.start_background_scan())
            index.stop()
            index.join(timeout=2.0)
            self.assertFalse(index.status()["running"])

    def test_partial_scan_does_not_purge_existing_catalog(self) -> None:
        with workspace_tempdir() as root:
            media = root / "media"
            media.mkdir()
            (media / "song.mp3").write_bytes(b"fake")
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([media], root / "write", 5, db_path=root / "catalog.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            index.rescan()
            self.assertEqual(index.db.count("audio"), 1)

            missing = root / "temporarily-offline-drive"
            index.config = replace(config, library=replace(config.library, read_folders=[missing]))
            index.rescan()
            self.assertEqual(index.db.count("audio"), 1)
            self.assertEqual(index.status()["phase"], "partial")

    def test_failed_reconfigure_restores_old_index_scan(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "one.db"))
            index = MediaIndex(config, probe_fn=fake_probe)
            other = replace(config, library=replace(config.library, db_path=root / "two.db"))
            with mock.patch("server.poko_server.media_index.PokoDatabase", side_effect=OSError("disk failed")):
                with self.assertRaises(OSError):
                    index.reconfigure(other)
            index.join(timeout=2)
            self.assertEqual(index.config, config)
            self.assertEqual(index.db.path, root / "one.db")
            self.assertEqual(index.status()["phase"], "idle")

    def test_reconfigure_switches_database_path(self) -> None:
        with workspace_tempdir() as root:
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            first = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "one.db"))
            second = replace(first, library=replace(first.library, db_path=root / "two.db"))
            index = MediaIndex(first, probe_fn=fake_probe)
            index.reconfigure(second, restart_scan=False)
            self.assertEqual(index.db.path, root / "two.db")
            self.assertEqual(index.config.library.db_path, root / "two.db")

    @unittest.skipIf(os.path.normcase("A") == os.path.normcase("a"), "case-insensitive platform")
    def test_ids_do_not_collide_for_case_distinct_paths(self) -> None:
        with workspace_tempdir() as root:
            upper = root / "Song.mp3"
            lower = root / "song.mp3"
            upper.write_bytes(b"a")
            lower.write_bytes(b"b")
            self.assertNotEqual(MediaIndex._id_for(upper), MediaIndex._id_for(lower))

    def test_probe_failure_is_reported_without_crashing_scan(self) -> None:
        with workspace_tempdir() as root:
            (root / "bad.mp3").write_bytes(b"bad")
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, probe_on_scan=True, db_path=root / "catalog.db"))
            index = MediaIndex(config, probe_fn=lambda path, ffprobe: (_ for _ in ()).throw(RuntimeError("probe boom")))
            index.rescan()
            status = index.status()
            self.assertEqual(status["phase"], "partial")
            self.assertTrue(any("probe boom" in err for err in status["errors"]))
            self.assertEqual(index.db.count(), 0)

    @unittest.skipIf(os.path.normcase("A") == os.path.normcase("a"), "case-insensitive platform")
    def test_legacy_lowercase_id_collision_migrates_without_losing_case_distinct_files(self) -> None:
        with workspace_tempdir() as root:
            upper = (root / "Song.mp3").resolve()
            lower = (root / "song.mp3").resolve()
            upper.write_bytes(b"upper")
            lower.write_bytes(b"lower")
            base = load_config(Path(__file__).resolve().parents[1] / "config.example.yml")
            config = replace(base, library=LibraryConfig([root], root / "write", 5, db_path=root / "catalog.db"))
            index = MediaIndex(config, probe_fn=fake_probe)

            legacy_id = hashlib.sha1(str(upper).lower().encode("utf-8")).hexdigest()[:16]
            index.db.upsert_item(
                MediaItem(id=legacy_id, kind="audio", path=upper, title="Upper", extension=".mp3", size_bytes=5),
                "legacy",
                enriched=False,
            )
            # Force the historical collision directly. This stays valid even when
            # the temporary parent directory itself contains uppercase letters.
            index.db.upsert_item(
                MediaItem(id=legacy_id, kind="audio", path=lower, title="Lower", extension=".mp3", size_bytes=5),
                "migration",
                enriched=False,
            )
            # A normal rescan then converges both rows onto the current ID scheme.
            index.db.upsert_item(
                MediaItem(id=MediaIndex._id_for(lower), kind="audio", path=lower, title="Lower", extension=".mp3", size_bytes=5),
                "new",
                enriched=False,
            )
            index.db.upsert_item(
                MediaItem(id=MediaIndex._id_for(upper), kind="audio", path=upper, title="Upper", extension=".mp3", size_bytes=5),
                "new",
                enriched=False,
            )

            self.assertEqual(index.db.count("audio"), 2)
            paths = {str(item.path) for item in index.db.page("audio", limit=10, offset=0)}
            self.assertEqual(paths, {str(upper), str(lower)})


if __name__ == "__main__":
    unittest.main()

