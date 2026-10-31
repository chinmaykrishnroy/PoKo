"""Opt-in live PCM observation check. Start continuous Music or SSync first.

Opens both tools and changes their modes without stopping the existing stream.
Targets and credentials come from the private verification .env. No audio is
recorded or returned by this test.
"""
import json
import time
import urllib.request
from verification_config import verification_parser, load_verification_env, target_url, secret_directory


def main():
    parser = verification_parser(__doc__)
    parser.add_argument("--source", choices=("music", "ssync"), required=True)
    args = parser.parse_args()
    values = load_verification_env(args.env)
    base = target_url(values, "POKO_TEST_DEVICE_URL")
    password = (secret_directory(values) / "device-admin-password").read_text().strip()
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(None, base, "admin", password)
    opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))

    def get(path):
        with opener.open(base + path, timeout=20) as response:
            return json.load(response)

    original = get("/api/health")
    assert original["firmware_version"] == values["POKO_TEST_EXPECTED_VERSION"]
    initial_snap = get("/api/snap")
    expected = args.source.upper()

    def playing():
        if expected == "MUSIC":
            return get("/api/health").get("music_playing", False)
        snap = get("/api/snap")
        # Ownership alone is also true for an idle, silent Snapclient.
        return (snap.get("connected") and not snap.get("suspended")
                and snap.get("audio_active") and snap.get("playing"))

    assert playing(), "Start continuous " + expected + " playback before running this test"
    reports = []
    try:
        for name, modes in (("analyzer", 4), ("tuner", 2)):
            get("/api/power?screen=on")
            get("/api/app?set=" + name)
            time.sleep(1.5)
            first = get("/api/acoustic")
            assert first["input_source"] == expected and first["capturing"], first["input_source"]
            time.sleep(3)
            last = get("/api/acoustic")
            elapsed = ((last["captured_at_ms"] - first["captured_at_ms"]) & 0xffffffff) / 1000
            assert elapsed > 0
            rate = ((last["frames"] - first["frames"]) & 0xffffffff) / elapsed
            assert abs(rate - last["sample_rate"]) < last["sample_rate"] * 0.03, rate
            assert last["input_overruns"] == first["input_overruns"], "PCM observer fell behind"
            visited = set()
            for _ in range(modes):
                state = get("/api/acoustic")
                assert state["input_source"] == expected and state["capturing"] and not state["error"]
                assert playing(), "Opening/changing tool interrupted playback"
                assert get("/api/snap").get("suspended") == initial_snap.get("suspended")
                visited.add(state["mode"])
                get("/api/power?screen=on")
                get("/api/input?action=enter")
                time.sleep(0.3)
            assert len(visited) == modes
            reports.append({"app": name, "source": expected, "modes": sorted(visited),
                            "sample_rate": last["sample_rate"], "observed_frames_per_second": round(rate),
                            "observer_overruns": last["input_overruns"]})
            get("/api/app?set=home")
            assert playing(), "Closing tool interrupted playback"
        assert get("/api/health")["uptime_ms"] > original["uptime_ms"], "Device rebooted"
        print(json.dumps({"passed": reports}, indent=2))
    finally:
        safe = original["app_state"] if original["app_state"] in (0, 1, 2, 3, 4, 7, 8) else 0
        get("/api/power?screen=on")
        get("/api/app?state=" + str(safe))


if __name__ == "__main__":
    main()
