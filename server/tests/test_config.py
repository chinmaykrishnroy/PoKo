from __future__ import annotations

import unittest
from pathlib import Path

from server.poko_server.config import load_config, load_raw_config
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


if __name__ == "__main__":
    unittest.main()
