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
import argparse

parser = argparse.ArgumentParser(description="Pair your device on a trusted LAN")
parser.add_argument("host", help="Device LAN IP or hostname")
parser.add_argument("--server-url", required=True, help="Your HTTPS media server URL")
args = parser.parse_args()
parsed = urllib.parse.urlsplit(args.server_url)
if (parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or
        parsed.path not in {"", "/"} or parsed.query or parsed.fragment):
    parser.error("--server-url must be an HTTPS origin without credentials or a path")
host = args.host
if any(c in host for c in "/:@ "):
    parser.error("host must be a LAN IP or hostname")
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
    "addr": args.server_url.rstrip("/"), "token": token, "ca": "",
}).encode(), method="POST")
with opener.open(request, timeout=15) as response:
    assert json.load(response)["ok"]
with opener.open(base + "/api/health", timeout=10) as response:
    data = json.load(response)
    print("Device paired:", data.get("firmware_version"), data.get("server_addr"))
