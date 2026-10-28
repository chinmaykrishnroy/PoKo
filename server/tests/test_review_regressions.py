import threading
import unittest
import urllib.error
import urllib.request
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from helpers import workspace_tempdir
from server.poko_server.config import LibraryConfig, load_config
from server.poko_server.http_api import make_server
from server.poko_server.thumbnail_cache import thumbnail_cached


class ReviewRegressions(unittest.TestCase):
    def test_thumbnail_cache_reuses_and_invalidates_after_replacement(self):
        with workspace_tempdir() as root:
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, library=LibraryConfig([root], root, 5, db_path=root / "db"))
            image = root / "photo.jpg"
            image.write_bytes(b"first")
            with patch("server.poko_server.thumbnail_cache._pending", {}):
                producer = unittest.mock.Mock(return_value={"data": "first"})
                self.assertEqual(thumbnail_cached(image, "image", config, 80, "test", producer), {"data": "first"})
                thumbnail_cached(image, "image", config, 80, "test", producer)
                self.assertEqual(producer.call_count, 1)
                image.write_bytes(b"replacement-longer")
                thumbnail_cached(image, "image", config, 80, "test", producer)
                self.assertEqual(producer.call_count, 2)
    def test_failed_playback_controls_are_not_http_success(self):
        with workspace_tempdir() as root:
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0,
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "db"))
            server = make_server(config, dry_run=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                for path in ["/api/playback/seek?seconds=10", "/api/playback/next", "/api/playback/previous"]:
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(f"http://127.0.0.1:{server.server_address[1]}{path}")
                    self.assertEqual(error.exception.code, 409)
            finally:
                server.shutdown()
                server.server_close()

    def test_interactive_page_size_is_bounded(self):
        with workspace_tempdir() as root:
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, host="127.0.0.1", port=0,
                             library=LibraryConfig([root], root / "write", 5, db_path=root / "db"))
            server = make_server(config, dry_run=True)
            try:
                page = server.RequestHandlerClass.backend.index.page("audio", 1, 1000000)
                self.assertLessEqual(page["page_size"], 50)
            finally:
                server.server_close()
