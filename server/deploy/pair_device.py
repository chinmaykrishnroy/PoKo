"""Run on the Pi as root after first flashing, within five minutes of boot.

Sends separate admin and media credentials over the user's trusted local LAN.
No credential is printed or included in a URL. Future device requests use
Digest authentication and server requests use verified HTTPS + device token.
"""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.3"
base = "http://" + host
password = Path("/etc/poko/device-admin-password").read_text().strip()
token = Path("/etc/poko/device-token").read_text().strip()
try:
    request = urllib.request.Request(base + "/api/security/setup", data=urllib.parse.urlencode({"password": password}).encode(), method="POST")
    urllib.request.urlopen(request, timeout=10).close()
except urllib.error.HTTPError as exc:
    if exc.code != 401:
        raise
manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
manager.add_password(None, base, "admin", password)
opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
request = urllib.request.Request(base + "/api/server", data=urllib.parse.urlencode({
    "addr": "https://poko.prefect-sys.online:443", "token": token, "ca": "",
}).encode(), method="POST")
with opener.open(request, timeout=15) as response:
    assert json.load(response)["ok"]
with opener.open(base + "/api/health", timeout=10) as response:
    data = json.load(response)
    print("Device paired:", data.get("firmware_version"), data.get("server_addr"))
