"""Post-flash hardware checks. Keep credentials on the Pi; restore original UI.
These opt-in live checks verify state/counters, not subjective sound. They
temporarily change app/volume/playback and restore app and volume afterward.
"""
import base64
import json
from pathlib import Path
import time
import urllib.request
import urllib.parse
from verification_config import verification_parser, load_verification_env, target_url, secret_directory

args = verification_parser(__doc__).parse_args()
settings = load_verification_env(args.env)
base = target_url(settings, "POKO_TEST_DEVICE_URL")
public = target_url(settings, "POKO_TEST_SERVER_URL", https=True)
root = secret_directory(settings)
password = (root / "device-admin-password").read_text().strip()
token = (root / "device-token").read_text().strip()
admin = (root / "admin-password").read_text().strip()
manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
manager.add_password(None, base, "admin", password)
opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))

def device(path):
    with opener.open(base + path, timeout=20) as response:
        return json.load(response)

def server(path, admin_role=False):
    auth = ("Basic " + base64.b64encode(("admin:" + admin).encode()).decode()) if admin_role else "Bearer " + token
    request = urllib.request.Request(public + path, headers={"Authorization": auth, "User-Agent": "ESP32HTTPClient"})
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.load(response)

health = device("/api/health")
expected = settings.get("POKO_TEST_EXPECTED_VERSION")
if not expected:
    raise ValueError("Set POKO_TEST_EXPECTED_VERSION in your .env file")
assert health["firmware_version"] == expected, health["firmware_version"]
assert health["server_host"] == urllib.parse.urlsplit(public).hostname
print("Firmware verified", health["firmware_version"], "heap", health["heap_free"])
if settings.get("POKO_TEST_PLAYBACK") != "1":
    print("Playback checks skipped; set POKO_TEST_PLAYBACK=1 to opt in")
    raise SystemExit(0)
catalog = server("/api/library/video?page_size=1&icons=false")
assert catalog.get("items") and catalog["items"][0].get("duration_s", 0) > 30, "Use a video longer than 30 seconds for live verification"
assert server("/api/library/audio?page_size=1&icons=false").get("items"), "Add a song before live verification"
original_app = health["app_state"]
original_master = health["master_vol"]
original_audio = device("/api/power").get("protected_activity_source")
volumes = {}
try:
    # Keep test audio low without altering the user's source volume settings.
    device("/api/sys?master_vol=10")
    device("/api/audio/stop")
    device("/api/power?screen=on")
    device("/api/app?set=home")
    device("/api/app?set=music")
    print("Music selected", device("/api/health")["app_state"], flush=True)
    device("/api/power?screen=on")
    device("/api/input?action=enter")
    time.sleep(6)
    power = device("/api/power")
    state = server("/api/playback/status")
    print("Music:", json.dumps({"protected": power["protected_activity_source"], "locks": power["locks"], "server_active": state.get("active")}))
    assert power["protected_activity_source"] == "MUSIC", power
    assert state.get("active"), state
    device("/api/audio/stop")

    # Test through device navigation to exercise its own HTTPS/token requests.
    server("/api/rescan", True)
    for _ in range(30):
        if not server("/api/index/status", True)["index"]["running"]:
            break
        time.sleep(1)
    device("/api/power?screen=on")
    device("/api/app?set=video")
    device("/api/power?screen=on")
    device("/api/input?action=enter")
    time.sleep(4)
    video = device("/api/health")
    power = device("/api/power")
    print("Video:", json.dumps({"source": power["protected_activity_source"], "fps": video.get("video_render_fps"), "underruns": video.get("audio_underruns"), "drops": video.get("video_frames_dropped")}))
    assert power["protected_activity_source"] == "VIDEO", power
    assert video.get("video_render_fps", 0) > 0, video
    device("/api/input?action=right")
    time.sleep(3)
    assert device("/api/power")["protected_activity_source"] == "VIDEO"
    print("PASS next-video transition")
finally:
    try:
        server("/api/playback/stop?notify=false")
        device("/api/audio/stop")
        device("/api/sys?master_vol=" + str(original_master))
        if original_audio == "SSYNC":
            device("/api/snap?action=start")
        device("/api/app?state=" + str(original_app))
    except Exception as exc:
        print("Restore needs inspection:", type(exc).__name__)
