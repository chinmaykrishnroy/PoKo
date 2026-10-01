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
    for script in ("prepare_audio_test.py", "prepare_video_test.py"):
        subprocess.run([sys.executable, str(HERE / script)], cwd=ROOT, check=True)
    cases = {
        "buttons": ["deferred-init", "double-left", "double-right", "info-hold", "power-click",
                    "wake-left", "wake-right", "wake-power", "music-hold", "single-left", "single-right",
                    "busy-main-loop", "bounce", "overflow", "combo-click", "combo-double",
                    "combo-reboot", "combo-drivers", "combo-emergency"],
        "audio": ["suspend-transition", "suspend-timeout", "stop-all"],
    "video": ["next", "next-page", "page-failure", "http-failure", "missing-refresh", "missing-empty", "load-failure", "stop-timeout", "next-stop-timeout"],
        "marquee": ["scroll-and-wrap"],
        "pixel-toggle": ["preserves-color", "forces-solid", "turns-off"],
        "power-policy": ["media-inhibits", "threshold-delay", "above-threshold", "charging-inhibits", "disabled"],
    }
    sources = {
        "buttons": [HERE / "buttons.cpp", args.onebutton / "OneButton.cpp"],
        "audio": [ROOT / "build/audio-test.cpp"],
        "video": [ROOT / "build/video-test.cpp"],
        "marquee": [HERE / "marquee.cpp"],
        "pixel-toggle": [HERE / "pixel_toggle.cpp"],
        "power-policy": [HERE / "power_policy.cpp"],
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
        if vcvars:
            command = ["cl", "/nologo", "/EHsc", "/permissive-", "/std:c++17"]
            command += ["/I" + str(path) for path in includes] + [str(path) for path in files]
            command += ["/Fe" + str(exe), "/Fo" + str(BUILD) + "\\", "/link", "/INCREMENTAL:NO"]
            batch = BUILD / ("compile-" + name + ".cmd")
            batch.write_text('@echo off\ncall "' + str(vcvars) + '" >nul\n' + subprocess.list2cmdline(command) + '\n')
            subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=ROOT, check=True, timeout=120)
        else:
            command = [compiler, "-std=c++17", "-pthread"] + ["-I" + str(path) for path in includes]
            subprocess.run(command + [str(path) for path in files] + ["-o", str(exe)], cwd=ROOT, check=True, timeout=120)
        for case in cases[name]:
            print(f"{name}: {case}", flush=True)
            subprocess.run([str(exe), case], cwd=ROOT, check=True, timeout=10)
    print(f"PASS: {sum(map(len, cases.values()))} firmware regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
