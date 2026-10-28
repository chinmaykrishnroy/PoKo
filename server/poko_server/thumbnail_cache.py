"""Bounded, persistent, modification-aware thumbnail cache.

At most two producers run or queue. HTTP workers return a placeholder if a
thumbnail is not ready quickly; a later request picks up the completed result.
"""
import hashlib
import json
import os
import threading
from concurrent.futures import ThreadPoolExecutor, TimeoutError
from pathlib import Path

_workers = ThreadPoolExecutor(max_workers=2, thread_name_prefix="thumbnail")
_lock = threading.Lock()
_pending = {}


def thumbnail_cached(path: Path, kind: str, config, size: int, variant: str, producer):
    try:
        stat = path.stat()
        identity = (str(path.resolve()), stat.st_mtime_ns, stat.st_size, kind, size,
                    variant, config.ffmpeg.executable, "v1")
        key = hashlib.sha256(repr(identity).encode()).hexdigest()
        directory = config.library.db_path.parent / "thumbnails"
        directory.mkdir(parents=True, exist_ok=True)
        entry = directory / (key + ".json")
        if entry.exists():
            return json.loads(entry.read_text())
    except (OSError, ValueError):
        return None

    def generate():
        try:
            result = producer()
            temporary = entry.with_suffix(".tmp")
            temporary.write_text(json.dumps(result))
            os.replace(temporary, entry)
            # Bound disk usage too, including superseded modification keys.
            entries = sorted(directory.glob("*.json"), key=lambda file: file.stat().st_mtime)
            for old in entries[:-256]:
                old.unlink(missing_ok=True)
            return result
        except OSError:
            return None

    with _lock:
        future = _pending.get(key)
        if future is None:
            if len(_pending) >= 2:
                return None
            future = _workers.submit(generate)
            _pending[key] = future

    def done(_):
        with _lock:
            _pending.pop(key, None)

    # Attach outside the lock: an already-finished future runs this immediately.
    future.add_done_callback(done)
    try:
        return future.result(timeout=0.35)
    except TimeoutError:
        return None
