"""Probe public TLS/authentication without displaying any credentials."""
import base64
from pathlib import Path
import urllib.request
import urllib.error
from verification_config import verification_parser, load_verification_env, target_url, secret_directory

args = verification_parser(__doc__).parse_args()
settings = load_verification_env(args.env)
base = target_url(settings, "POKO_TEST_SERVER_URL", https=True)
root = secret_directory(settings)
admin = "Basic " + base64.b64encode(("admin:" + (root / "admin-password").read_text().strip()).encode()).decode()
device = "Bearer " + (root / "device-token").read_text().strip()
for auth, path, expected in [(None, "/health", 401), (admin, "/health", 200),
                             (device, "/health", 200), (device, "/api/server/config", 403),
                             (device, "/api/library/audio?icons=false&enrich=false&page_size=1", 200)]:
    headers = {"User-Agent": "ESP32HTTPClient"}
    if auth:
        headers["Authorization"] = auth
    request = urllib.request.Request(base + path, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            status = response.status
    except urllib.error.HTTPError as error:
        status = error.code
        if status != expected:
            print("Unexpected response type:", error.headers.get("Content-Type"),
                  "challenge:", error.headers.get("cf-mitigated"))
    print(path, "status=", status, "expected=", expected)
    assert status == expected
print("PASS public TLS/admin/device credential boundaries")
