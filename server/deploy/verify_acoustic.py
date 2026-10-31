"""Opt-in live microphone checks; targets and credentials come from private .env.

Temporarily opens the microphone apps and stops foreground playback. Verifies
physical RX counters, all modes, PA-off and repeated ownership handoffs. It does
not claim acoustic accuracy without an external reference near the microphone.
"""
import json
import time
import urllib.request
from verification_config import verification_parser, load_verification_env, target_url, secret_directory


def main():
    args = verification_parser(__doc__).parse_args()
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
    initial_uptime = original["uptime_ms"]
    report = []
    try:
        # Auto input normally preserves playback. This opt-in test explicitly
        # stops it to exercise the physical microphone instead of the PCM tap.
        get("/api/audio/stop")
        get("/api/power?screen=on")
        for name, app_id, modes in (("analyzer", 9, 4), ("tuner", 10, 2)):
            get("/api/app?set=" + name)
            time.sleep(1)
            first = get("/api/acoustic")
            assert first["capturing"] and not first["error"], first
            assert first["input_source"] == "MIC" and first["sample_rate"] == 48000
            time.sleep(5)
            current = get("/api/acoustic")
            elapsed = ((current["captured_at_ms"] - first["captured_at_ms"]) & 0xffffffff) / 1000
            assert elapsed > 0
            rate = ((current["frames"] - first["frames"]) & 0xffffffff) / elapsed
            assert 47000 < rate < 49000, ("unexpected physical RX throughput", rate)
            assert current["rx_overruns"] == first["rx_overruns"], "microphone lost samples during steady capture"
            visited = set()
            for _ in range(modes):
                current = get("/api/acoustic")
                assert current["app_state"] == app_id and current["capturing"] and current["input_source"] == "MIC", current
                assert current["has_result"] and current["last_data_age_ms"] < 500, current
                assert not current["speaker_amp"] and not current["error"], current
                visited.add(current["mode"])
                get("/api/power?screen=on")
                get("/api/input?action=enter")
                time.sleep(0.4)
            assert len(visited) == modes, visited
            power = get("/api/power")
            assert power["protected_activity_source"] == "MIC", power["protected_activity_source"]
            report.append({"app": name, "modes": sorted(visited), "rx_frames_per_second": round(rate),
                           "rms_dbfs": current["rms_dbfs"], "speaker_amp": current["speaker_amp"]})
            get("/api/app?set=home")
            assert not get("/api/acoustic")["capturing"]

        heaps = []
        for _ in range(8):
            for name in ("analyzer", "tuner"):
                get("/api/app?set=" + name)
                time.sleep(0.2)
                assert get("/api/acoustic")["capturing"]
                get("/api/app?set=home")
                assert not get("/api/acoustic")["capturing"]
            health = get("/api/health")
            assert health["uptime_ms"] > initial_uptime, "device rebooted"
            heaps.append(health["heap_free"])
        print(json.dumps({"passed": report, "reopen_cycles": 16, "free_heap_samples": heaps}, indent=2))
    finally:
        # A streaming session requires an explicit new Play action after handoff.
        # Restore only a safe previous foreground, never restart media blindly.
        safe = original["app_state"] if original["app_state"] in (0, 1, 2, 5, 7, 8) else 0
        get("/api/power?screen=on")
        get("/api/app?state=" + str(safe))


if __name__ == "__main__":
    main()
