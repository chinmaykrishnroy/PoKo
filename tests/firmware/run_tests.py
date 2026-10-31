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
    parser.add_argument("--only", action="append", help="Run only this host group; repeat to select multiple")
    args = parser.parse_args()
    if not (args.onebutton / "OneButton.cpp").is_file():
        parser.error("Pass --onebutton with the installed OneButton source directory")
    BUILD.mkdir(parents=True, exist_ok=True)
    for script in ("prepare_audio_test.py", "prepare_microphone_test.py", "prepare_acoustic_app_test.py", "prepare_capture_power_test.py", "prepare_snap_ingress_test.py", "prepare_video_test.py", "prepare_music_test.py", "prepare_library_status_test.py", "prepare_power_wake_test.py"):
        subprocess.run([sys.executable, str(HERE / script)], cwd=ROOT, check=True)
    cases = {
        "ssync-ingress": ["full-suspended-ring", "fresh-after-long-idle"],
        "playback-tap": ["disabled-and-copy", "overflow-and-rate-change", "pause-discards-history", "silent-channel-handoff", "producer-consumer"],
        "capture-power": ["capture-pa-off-and-lock", "capture-stop-and-playback"],
        "acoustic-app": ["playback-preserved", "playback-rate-and-transition", "music-starts-during-microphone", "overrun-clears-reading", "auto-gain-persistence", "render-preview", "right-footer-and-fft-headroom", "quiet-placeholder", "scope-connected-trace", "modes-and-holds", "tuner-and-stale-input", "failure-and-retry", "external-stop-and-reload"],
        "microphone": ["no-clock-steal", "failure-cleanup", "capture-and-stop", "gain"],
        "launcher-window": ["scroll-and-wrap"],
        "acoustic-dsp": ["playback-sample-rates", "tuner-history-without-bass", "scope-trace-math", "fft-headroom", "reading-hysteresis", "quality-change-confirmation", "auto-gain", "semantic-colors", "bass-resolution", "bass-playback-clock", "harmonic-note-rejection", "pitch", "spectrum-levels", "chords", "silence-noise", "scope-period", "adaptive-scale", "weak-chord", "uneven-chord", "weak-subharmonic-chord", "open-voicing-chord", "rich-chords", "bright-chord-missing-third", "weak-fundamental-single-note", "pitch-with-transient", "scale-headroom"],
        "power-wake": ["held-power", "held-combo", "released", "rtc-restore-before-boot"],
        "boot-splash": ["white-first", "fusion-randomized", "allocation-fallback", "nonblocking-finish", "visible-duration", "slow-startup", "clock-wrap"],
        "library-status": ["empty", "offline", "error", "missing", "loading", "populated", "invalidated-cache"],
        "device-security": ["rotation-preserves-controls", "password-change-validation", "default-password", "storage-failure-keeps-admin", "setup-cannot-rotate", "metadata-auth-required",
                            "browser-no-native-prompt", "browser-session-tab-return", "browser-session-password-revocation", "browser-remember-cookie"],
        "buttons": ["deferred-init", "double-left", "double-right", "info-hold", "power-click",
                    "wake-left", "wake-right", "wake-power", "music-hold", "single-left", "single-right",
                    "busy-main-loop", "bounce", "overflow", "combo-click", "combo-double",
                    "combo-reboot", "combo-drivers", "combo-emergency"],
        "audio": ["mic-resumes-incoming-ssync", "mic-from-music", "mic-from-video", "mic-handoff", "mic-failure-restores-ssync", "mic-stop-all", "mic-handoff-timeout", "suspend-transition", "suspend-timeout", "stop-all", "silent-video-is-not-audio"],
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
        "ssync-ingress": [ROOT / "build/snap-ingress-test.cpp", ROOT / "AudioActivity.cpp"],
        "playback-tap": [HERE / "playback_audio_tap.cpp", ROOT / "PlaybackAudioTap.cpp"],
        "capture-power": [ROOT / "build/capture-power-test.cpp", ROOT / "PowerPolicy.cpp"],
        "acoustic-app": [ROOT / "build/acoustic-app-test.cpp", ROOT / "AcousticDSP.cpp", ROOT / "AcousticPresentation.cpp", ROOT / "PlaybackAudioTap.cpp"],
        "microphone": [ROOT / "build/microphone-test.cpp"],
        "launcher-window": [HERE / "launcher_window.cpp", ROOT / "LauncherWindow.cpp"],
        "acoustic-dsp": [HERE / "acoustic_dsp.cpp", ROOT / "AcousticDSP.cpp", ROOT / "AcousticPresentation.cpp"],
        "power-wake": [HERE / "power_wake.cpp"],
        "boot-splash": [HERE / "boot_splash.cpp", ROOT / "BootSplash.cpp"],
        "library-status": [HERE / "library_status.cpp"],
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
    if args.only and set(args.only) - set(sources):
        parser.error("Unknown host group: " + ", ".join(sorted(set(args.only) - set(sources))))
    for name, files in sources.items():
        if args.only and name not in args.only:
            continue
        exe = BUILD / (name + (".exe" if os.name == "nt" else ""))
        case_includes = ([HERE / "security_stubs"] if name == "device-security" else []) + includes
        if name in ("acoustic-app", "playback-tap"):
            case_includes.insert(0, HERE / "acoustic_stubs")
        if name == "boot-splash":
            case_includes.insert(0, HERE / "splash_stubs")
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
    print(f"PASS: {sum(len(group) for name, group in cases.items() if not args.only or name in args.only)} firmware regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
