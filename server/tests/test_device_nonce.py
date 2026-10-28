import concurrent.futures
from dataclasses import replace
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import threading
import time
import unittest
from unittest.mock import patch

from server.poko_server.config import load_config
from server.poko_server.device import DeviceClient


class DeviceNonceTests(unittest.TestCase):
    def test_parallel_device_actions_serialize_shared_digest_challenges(self):
        lock = threading.Lock()
        current = [0]
        class SingleNonceDevice(BaseHTTPRequestHandler):
            def do_GET(self):
                auth = re.search(r'nonce="([^"]+)"', self.headers.get("Authorization", ""))
                with lock:
                    accepted = auth and auth.group(1) == str(current[0])
                    if not accepted:
                        current[0] += 1
                    nonce = str(current[0])
                if accepted:
                    payload = b'{"ok":true}'
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                else:
                    time.sleep(0.03)
                    self.send_response(401)
                    self.send_header("WWW-Authenticate", f'Digest realm="PoKo", nonce="{nonce}", opaque="test", qop="auth"')
                    self.send_header("Content-Length", "0")
                    self.end_headers()
            def log_message(self, *args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), SingleNonceDevice)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            config = load_config(Path(__file__).parents[1] / "config.example.yml")
            config = replace(config, poko=replace(config.poko, base_url=f"http://127.0.0.1:{server.server_address[1]}"))
            with patch.dict(os.environ, {"POKO_DEVICE_PASSWORD": "synthetic-test-password"}):
                client = DeviceClient(config)
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    statuses = list(pool.map(lambda _: client.request("/api/health", timeout=5)[0], range(16)))
                self.assertEqual(statuses, [200] * 16)
        finally:
            server.shutdown()
            server.server_close()
