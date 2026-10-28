"""Extract production header rendering, including empty/error/count branches."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
methods = []
for app, boundary in [("VideoApp", "// Single Video Card Frame"),
                      ("MusicApp", "if (_mode == MODE_BROWSE)")]:
    source = (root / (app + ".cpp")).read_text(encoding="utf-8")
    start = source.index(f"void {app}::renderToCanvas()")
    end = source.index(boundary, start)
    methods.append(source[start:end] + "}\n")
(root / "build/library-status-methods.inc").write_text("\n".join(methods), encoding="utf-8")
