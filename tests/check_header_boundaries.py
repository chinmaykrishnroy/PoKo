"""Keep migrated firmware modules declaration-only."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MIGRATED = (
    "AudioManager",
    "BatteryManager",
    "ButtonInput",
    "ClockApp",
    "GalleryApp",
    "InfoApp",
    "MusicApp",
    "PixelApp",
    "PixelEngine",
    "PokoAPI",
    "PokoAppState",
    "PokoDrivers",
    "PokoOTA",
    "PokoOTAWeb",
    "PokoTheme",
    "PokoUI",
    "PokoWebUI",
    "PowerManager",
    "PowerPolicy",
    "SettingsApp",
    "SnapPlayer",
    "SSyncApp",
    "SyncedAVPlayer",
    "TCPAudio",
    "TitleMarquee",
    "VideoApp",
)

# Templates and third-party C interfaces are valid header-only exceptions.
HEADER_EXCEPTIONS = {"PixelToggle", "PokoPins", "es8311", "es8311_reg"}


def main() -> int:
    errors: list[str] = []
    function_body = re.compile(r"\)\s*(?:const\s*)?\{")
    for name in MIGRATED:
        header = ROOT / f"{name}.h"
        implementation = ROOT / f"{name}.cpp"
        if not implementation.is_file():
            errors.append(f"{name}: missing {implementation.name}")
            continue
        text = header.read_text(encoding="utf-8")
        check_text = text
        if name == "ButtonInput":
            check_text = re.sub(
                r"template<Kind kind>\s+static void callback\(\)\s*\{[^{}]*\}",
                "template<Kind kind> static void callback();",
                check_text,
            )
        if function_body.search(check_text):
            errors.append(f"{name}: function implementation found in header")
        if re.search(r"^\s*inline\s+", check_text, re.MULTILINE):
            errors.append(f"{name}: inline implementation found in header")
        source_text = implementation.read_text(encoding="utf-8")
        if re.search(r"^\s*(?:virtual|explicit|friend)\s+", source_text, re.MULTILINE):
            errors.append(f"{name}: declaration-only specifier found on source definition")
        if re.search(r"\)\s+(?:override|final)\s*\{", source_text):
            errors.append(f"{name}: override/final specifier found on source definition")
    known = set(MIGRATED) | HEADER_EXCEPTIONS
    for header in ROOT.glob("*.h"):
        if header.stem not in known:
            errors.append(f"{header.name}: new header must have a .cpp implementation or an explicit template/vendor exception")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"PASS: {len(MIGRATED)} migrated headers are declaration-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
