"""Compile the real AudioManager core; replace only hardware/RTOS dependencies."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
header = (root / "AudioManager.h").read_text(encoding="utf-8")
source = (root / "AudioManager.cpp").read_text(encoding="utf-8")
declarations = header[header.index("enum AudioSource"):header.index("    bool isPlaying() const;")]
definitions = source[source.index("AudioManager* AudioManager::_instance"):source.index("bool AudioManager::isPlaying() const")]
output = root / "build/audio-test.cpp"
output.write_text(
    '#include "audio_fixture.h"\n' + declarations + '};\n' + definitions +
    '\n#include "audio_main.cpp"\n',
    encoding="utf-8",
)

