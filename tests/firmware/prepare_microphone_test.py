"""Compile production ES7210 driver with only bus/GPIO dependencies replaced."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
header = (root / "MicrophoneInput.h").read_text(encoding="utf-8").replace("#include <driver/i2s_std.h>", "")
source = (root / "MicrophoneInput.cpp").read_text(encoding="utf-8")
for include in ('"MicrophoneInput.h"', '"PokoDrivers.h"', '"PokoPins.h"', '<Wire.h>'):
    source = source.replace(f"#include {include}", "")
(root / "build/microphone-test.cpp").write_text(
    '#include "microphone_fixture.h"\n' + header + source + '\n#include "microphone_main.cpp"\n', encoding="utf-8")
