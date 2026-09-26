from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent
    sys.path.insert(0, str(root.parent))
    temp_root = root / ".test-tmp"
    temp_root.mkdir(exist_ok=True)
    tempfile.tempdir = str(temp_root)
    suite = unittest.defaultTestLoader.discover(str(root / "tests"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
