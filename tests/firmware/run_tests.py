"""Host regressions using production firmware methods and the real OneButton library."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "host-tests"


def main() -> int:
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--onebutton", type=Path, default=Path.home() / "Documents/Arduino/libraries/OneButton/src")
    args = parser.parse_args()
    if not (args.onebutton / "OneButton.cpp").is_file():
        parser.error("Pass --onebutton with the installed OneButton source directory")
    BUILD.mkdir(parents=True, exist_ok=True)
    for script in ("prepare_audio_test.py", "prepare_video_test.py", "prepare_music_test.py"):
        subprocess.run([sys.executable, str(HERE / script)], cwd=ROOT, check=True)
    cases = {
        "device-security": ["rotation-preserves-controls", "password-change-validation", "storage-failure-keeps-admin", "setup-cannot-rotate", "metadata-auth-required",
                            "browser-no-native-prompt", "browser-session-tab-return", "browser-session-password-revocation", "browser-remember-cookie"],
        "buttons": ["deferred-init", "double-left", "double-right", "info-hold", "power-click",
                    "wake-left", "wake-right", "wake-power", "music-hold", "single-left", "single-right",
                    "busy-main-loop", "bounce", "overflow", "combo-click", "combo-double",
                    "combo-reboot", "combo-drivers", "combo-emergency"],
        "audio": ["suspend-transition", "suspend-timeout", "stop-all", "silent-video-is-not-audio"],
        "music": ["terminal-release", "handoff-defers-release", "handoff-timeout-retries", "shutdown-timeout-retains-owner", "startup-timeout-releases", "blocked-loop-clock", "remote-metadata", "remote-listener-failure", "server-handoff-no-stop"],
        "video": ["next", "next-page", "page-failure", "http-failure", "missing-refresh", "missing-empty", "load-failure", "stop-timeout", "next-stop-timeout", "fit-query", "async-rejected", "async-failed", "async-stale-result", "remote-metadata", "remote-listener-failure", "server-handoff-no-stop"],
        "marquee": ["scroll-and-wrap"],
        "pixel-toggle": ["preserves-color", "forces-solid", "turns-off"],
        "power-policy": ["active-above-threshold-inhibits", "inactive-delay",
                         "low-battery-overrides-activity", "charging-idle-still-powers-off", "disabled",
                         "display-independent-timeouts", "idle-power-stages",
                         "continuous-media-blocks-all-stages",
                         "media-stop-starts-idle-progression",
                         "media-low-battery-override",
                         "light-sleep-next-deadline",
                         "network-wake-defers-actions"],
        "battery": ["periodic-read-keeps-plug-event", "percentage-slew"],
        "audio-activity": ["silence", "audible-positive", "audible-negative"],
    }
    sources = {
        "device-security": [HERE / "device_security.cpp", ROOT / "DeviceSecurity.cpp", ROOT / "DeviceAccessPolicy.cpp", ROOT / "DeviceWebSession.cpp"],
        "buttons": [HERE / "buttons.cpp", ROOT / "ButtonInput.cpp", args.onebutton / "OneButton.cpp"],
        "audio": [ROOT / "build/audio-test.cpp"],
        "music": [ROOT / "build/music-test.cpp"],
        "video": [ROOT / "build/video-test.cpp", ROOT / "TitleMarquee.cpp"],
        "marquee": [HERE / "marquee.cpp", ROOT / "TitleMarquee.cpp"],
        "pixel-toggle": [HERE / "pixel_toggle.cpp"],
        "power-policy": [HERE / "power_policy.cpp", ROOT / "PowerPolicy.cpp"],
        "battery": [HERE / "battery.cpp", ROOT / "BatteryManager.cpp"],
        "audio-activity": [HERE / "audio_activity.cpp", ROOT / "AudioActivity.cpp"],
    }
    # Real OneButton must precede stubs/OneButton.h. Only Arduino hardware is mocked.
    includes = [args.onebutton, HERE / "stubs", HERE, ROOT]
    vcvars = None
    if os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if vswhere.is_file():
            install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                              "-property", "installationPath"], text=True).strip()
            if install:
                vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not vcvars and not compiler:
        parser.error("MSVC Build Tools, g++, or clang++ is required")
    for name, files in sources.items():
        exe = BUILD / (name + (".exe" if os.name == "nt" else ""))
        case_includes = ([HERE / "security_stubs"] if name == "device-security" else []) + includes
        if vcvars:
            command = ["cl", "/nologo", "/EHsc", "/permissive-", "/std:c++17"]
            command += ["/I" + str(path) for path in case_includes] + [str(path) for path in files]
            command += ["/Fe" + str(exe), "/Fo" + str(BUILD) + "\\", "/link", "/INCREMENTAL:NO"]
            batch = BUILD / ("compile-" + name + ".cmd")
            batch.write_text('@echo off\ncall "' + str(vcvars) + '" >nul\n' + subprocess.list2cmdline(command) + '\n')
            subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=ROOT, check=True, timeout=120)
        else:
            command = [compiler, "-std=c++17", "-pthread"] + ["-I" + str(path) for path in case_includes]
            subprocess.run(command + [str(path) for path in files] + ["-o", str(exe)], cwd=ROOT, check=True, timeout=120)
        for case in cases[name]:
            print(f"{name}: {case}", flush=True)
            subprocess.run([str(exe), case], cwd=ROOT, check=True, timeout=10)
    print(f"PASS: {sum(map(len, cases.values()))} firmware regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
