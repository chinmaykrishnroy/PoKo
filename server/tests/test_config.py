from __future__ import annotations

import unittest
from pathlib import Path
from dataclasses import replace

from server.poko_server.config import DisplayConfig, LibraryConfig, _parse_simple_yaml, config_to_yaml, load_config, load_raw_config
from helpers import workspace_tempdir


class ConfigTests(unittest.TestCase):
    def test_simple_yaml_parser_handles_project_config_shape(self) -> None:
        text = """
server:
  host: 127.0.0.1
  port: 9999
library:
  read_folders:
    - D:\\Media
    - E:\\Other
  write_folder: D:\\Trash
  page_size: 5
nexus:
  ip: 192.168.0.10
  ports:
    graphics: 1234
"""
        with workspace_tempdir() as tmp:
            path = tmp / "config.yml"
            path.write_text(text, encoding="utf-8")
            raw = load_raw_config(path)
            self.assertEqual(raw["library"]["read_folders"], ["D:\\Media", "E:\\Other"])
            config = load_config(path)
            self.assertEqual(config.host, "127.0.0.1")
            self.assertEqual(config.port, 9999)
            self.assertEqual(config.library.page_size, 5)
            self.assertEqual(config.nexus.graphics_port, 1234)

    def test_config_round_trip_preserves_display_empty_lists_and_special_strings(self) -> None:
        with workspace_tempdir() as tmp:
            source = tmp / "source.yml"
            source.write_text(
                "server:\n  host: 127.0.0.1\nlibrary:\n  read_folders: []\n  probe_on_scan: \"false\"\n"
                "defaults:\n  video_with_audio: \"false\"\nffmpeg:\n  executable: \"ffmpeg # custom: build\"\n",
                encoding="utf-8",
            )
            base = load_config(source)
            self.assertFalse(base.library.probe_on_scan)
            self.assertFalse(base.defaults.video_with_audio)
            self.assertEqual(base.ffmpeg.executable, "ffmpeg # custom: build")

            custom = replace(
                base,
                host="host#1: test",
                display=DisplayConfig(128, 128),
                library=LibraryConfig([], tmp / "write # folder", 7, False, tmp / "db # one.sqlite"),
            )
            rendered = config_to_yaml(custom)
            self.assertEqual(rendered.count("sync_audio_chunk_ms:"), 1)
            target = tmp / "roundtrip.yml"
            target.write_text(rendered, encoding="utf-8")
            loaded = load_config(target)
            self.assertEqual(loaded.host, "host#1: test")
            self.assertEqual((loaded.display.width, loaded.display.height), (128, 128))
            self.assertEqual(loaded.library.read_folders, [])
            self.assertEqual(loaded.library.page_size, 7)
            self.assertIn("write # folder", str(loaded.library.write_folder))

    def test_fallback_parser_preserves_hash_inside_quotes_and_empty_list(self) -> None:
        parsed = _parse_simple_yaml('server:\n  host: "host#inside" # outside comment\nlibrary:\n  read_folders: []\n')
        self.assertEqual(parsed["server"]["host"], "host#inside")
        self.assertEqual(parsed["library"]["read_folders"], [])

    def test_fallback_parser_handles_yaml_escaped_single_quote_before_comment(self) -> None:
        parsed = _parse_simple_yaml("server:\n  host: 'it''s#inside' # outside comment\n")
        self.assertEqual(parsed["server"]["host"], "it's#inside")

    def test_protocol_incompatible_config_is_rejected(self) -> None:
        with workspace_tempdir() as tmp:
            path = tmp / "config.yml"
            path.write_text("poko:\n  switch_delay_ms: 0\n", encoding="utf-8")
            self.assertEqual(load_config(path).poko.switch_delay_ms, 0)
            for text in ("display:\n  width: 1920\n", "ffmpeg:\n  sync_audio_rate: 0\n", "ffmpeg:\n  sync_audio_rate: 32000\n"):
                path.write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    load_config(path)


if __name__ == "__main__":
    unittest.main()

