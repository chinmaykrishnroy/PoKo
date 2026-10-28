"""Extract production header rendering, including empty/error/count branches."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
methods = []
for app, boundary in [("VideoApp", "// Single Video Card Frame"),
                      ("MusicApp", "if (_mode == MODE_BROWSE)")]:
    source = (root / (app + ".cpp")).read_text(encoding="utf-8")
    start = source.index(f"void {app}::renderToCanvas()")
    end = source.index(boundary, start)
    methods.append(source[start:end] + "}\n")
    count = "_videoCount" if app == "VideoApp" else "_songCount"
    predicate = re.search(r"if \(([^\n]*" + count + r" > 0[^\n]*)\) \{", source).group(1)
    methods.append(f"bool {app}::cached(int index) {{ return {predicate}; }}\n")
(root / "build/library-status-methods.inc").write_text("\n".join(methods), encoding="utf-8")
