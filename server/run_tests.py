from __future__ import annotations

import sys
import tempfile
import threading
import unittest
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent
    sys.path.insert(0, str(root.parent))
    temp_root = root / ".test-tmp"
    temp_root.mkdir(exist_ok=True)
    tempfile.tempdir = str(temp_root)

    thread_exceptions: list[threading.ExceptHookArgs] = []
    orig_excepthook = threading.excepthook

    def custom_excepthook(args: threading.ExceptHookArgs) -> None:
        thread_exceptions.append(args)
        orig_excepthook(args)

    threading.excepthook = custom_excepthook

    suite = unittest.defaultTestLoader.discover(str(root / "tests"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)

    if thread_exceptions:
        print(f"\nFAIL: {len(thread_exceptions)} uncaught exception(s) occurred in background threads!", file=sys.stderr)
        return 1

    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())

