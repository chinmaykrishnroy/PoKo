"""Exercise production terminal cleanup and elapsed-time update before drawing."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = (root / "MusicApp.cpp").read_text()
methods = []
for name in ["stopAndReleaseMusic", "stopPlayback", "update"]:
    start = source.index(f"void MusicApp::{name}(")
    if name == "update":
        end = source.index("        if (!_active || !_canvas) return;", start)
        methods.append(source[start:end] + "}\n")
    else:
        pos = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[pos] == "{") - (source[pos] == "}")
            pos += 1
        methods.append(source[start:pos])
prefix = (root / "tests/firmware/music_fixture.h").read_text()
suffix = (root / "tests/firmware/music_main.cpp").read_text()
(root / "build/music-test.cpp").write_text(prefix + "\n".join(methods) + suffix)
