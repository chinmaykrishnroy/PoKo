"""Local deployment smoke checks; does not print credentials or media names."""
import base64
import json
from pathlib import Path
import urllib.error
import urllib.request

base = "http://127.0.0.1:8765"
password = Path("/etc/poko/admin-password").read_text().strip()
authorization = "Basic " + base64.b64encode(f"admin:{password}".encode()).decode()
for path in ["/", "/health", "/api/server/config", "/api/library/audio"]:
    try:
        urllib.request.urlopen(base + path, timeout=5)
        raise AssertionError(f"Unauthenticated access allowed: {path}")
    except urllib.error.HTTPError as error:
        assert error.code == 401, error.code
request = urllib.request.Request(base + "/health", headers={"Authorization": authorization})
with urllib.request.urlopen(request, timeout=10) as response:
    data = json.load(response)
    assert data["ok"]
    print("Authenticated health:", json.dumps(data))
request = urllib.request.Request(base + "/health", headers={"Authorization": authorization, "Origin": "https://evil.invalid"})
try:
    urllib.request.urlopen(request, timeout=5)
    raise AssertionError("Cross-origin access allowed")
except urllib.error.HTTPError as error:
    assert error.code == 403
print("PASS: anonymous=401 authenticated=200 cross-origin=403")
