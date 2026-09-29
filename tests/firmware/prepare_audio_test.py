"""Compile the real AudioManager core; replace only hardware/RTOS dependencies."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "AudioManager.h"
text = source.read_text(encoding="utf-8")
core = text[text.index("enum AudioSource"):text.index("    bool isPlaying() const")]
output = root / "build/audio-test.cpp"
output.write_text('#include "audio_fixture.h"\n' + core + '};\n#include "audio_main.cpp"\n', encoding="utf-8")

