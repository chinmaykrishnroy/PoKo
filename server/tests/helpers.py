from __future__ import annotations

import shutil
import threading
import uuid
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

_thread_exceptions: list[threading.ExceptHookArgs] = []
_orig_excepthook = threading.excepthook


def _tracking_excepthook(args: threading.ExceptHookArgs) -> None:
    _thread_exceptions.append(args)
    _orig_excepthook(args)


threading.excepthook = _tracking_excepthook


def get_thread_exceptions() -> list[threading.ExceptHookArgs]:
    return list(_thread_exceptions)


def clear_thread_exceptions() -> None:
    _thread_exceptions.clear()


@contextmanager
def workspace_tempdir() -> Iterator[Path]:
    root = Path(__file__).resolve().parents[1] / ".test-tmp"
    root.mkdir(exist_ok=True)
    path = root / f"case-{uuid.uuid4().hex}"
    path.mkdir(parents=True, exist_ok=False)
    try:
        yield path
    finally:
        shutil.rmtree(path, ignore_errors=True)
        if _thread_exceptions:
            errs = [f"{e.exc_type.__name__}: {e.exc_value} in {e.thread.name}" for e in _thread_exceptions]
            _thread_exceptions.clear()
            raise AssertionError(f"Uncaught exception(s) occurred in background threads: {', '.join(errs)}")

