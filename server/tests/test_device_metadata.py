import json
from pathlib import Path
import unittest
from unittest.mock import patch

from server.poko_server.config import load_config
from server.poko_server.device import DeviceClient, DeviceResponse
from server.poko_server.models import MediaItem


class DeviceMetadataTests(unittest.TestCase):
    def test_selected_identity_is_sent_and_acknowledged_before_start(self):
        client = DeviceClient(load_config(Path(__file__).parents[1] / "config.example.yml"))
        item = MediaItem(id="selected", kind="audio", path=Path("selected.mp3"), title="Selected song", artist="Artist", duration_s=177, extension=".mp3", size_bytes=1)
        with patch.object(client, "request", return_value=(200, b'{"ok":true,"media_id":"selected"}', "application/json")) as request, patch("server.poko_server.device.time.sleep"):
            self.assertTrue(client.prepare_media(item, 12, catalog_index=17, catalog_total=40).ok)
            self.assertEqual(request.call_args.args[0], "/api/playback/prepare")
            self.assertEqual(request.call_args.kwargs["method"], "POST")
            data = json.loads(request.call_args.kwargs["data"])
            self.assertEqual(data, {"kind":"audio","id":"selected","title":"Selected song","artist":"Artist","duration_s":177,"position_s":12,"catalog_index":17,"catalog_total":40})
        for status, body in [(200, b'{"ok":true,"media_id":"other"}'), (200, b'not json'), (401, b''), (404, b'')]:
            with patch.object(client, "request", return_value=(status, body, "text/plain")):
                self.assertFalse(client.prepare_media(item).ok)

    def test_explicit_stop_is_distinct_from_natural_end_of_stream(self):
        client = DeviceClient(load_config(Path(__file__).parents[1] / "config.example.yml"))
        with patch.object(client, "_get", return_value=DeviceResponse(True, "stopped")) as get:
            client.notify_playback_stopped()
            self.assertTrue(get.call_args.args[0].endswith("/api/ui/playback_stopped?reason=stop"))
