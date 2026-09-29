"""Exercise production video navigation/start/stop methods with transport doubles."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "VideoApp.h"
text = source.read_text(encoding="utf-8")
methods = []
for name in ("failPlayback", "requestPlay", "requestStopInternal", "requestStop", "selectVideo", "onLeft", "onRight"):
    marker = f"    {'bool' if name == 'selectVideo' else 'void'} {name}("
    if marker not in text:
        continue
    start = text.index(marker)
    pos = text.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    methods.append(text[start:end])
prefix = (root / "tests/firmware/video_fixture.h").read_text()
suffix = (root / "tests/firmware/video_main.cpp").read_text()
(root / "build/video-test.cpp").write_text(prefix + "\n".join(methods) + "\n};\n" + suffix)

