from __future__ import annotations

import hashlib
import math
import os
import threading
import time
from dataclasses import replace
from pathlib import Path
from typing import Any, Callable

from .config import AppConfig
from .database import PokoDatabase
from .ffmpeg_tools import (
    default_ffmpeg_executable,
    default_ffprobe_executable,
    ffprobe_media,
    media_kind_for,
    metadata_from_probe,
)
from .models import MediaItem


ProbeFn = Callable[[Path, str], dict[str, Any]]


class MediaIndex:
    def __init__(self, config: AppConfig, probe_fn: ProbeFn | None = None) -> None:
        self.config = config
        self.db = PokoDatabase(config.library.db_path)
        self._probe_fn = probe_fn or ffprobe_media
        self._ffprobe: str | None = None
        self._scan_thread: threading.Thread | None = None
        self._scan_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._status: dict[str, Any] = {
            "running": False,
            "phase": "idle",
            "current_folder": None,
            "current_path": None,
            "scanned_files": 0,
            "indexed_files": 0,
            "added": 0,
            "updated": 0,
            "removed": 0,
            "errors": [],
            "started_at": None,
            "finished_at": None,
        }

    def start_background_scan(self) -> bool:
        with self._scan_lock:
            if self._scan_thread and self._scan_thread.is_alive():
                return False
            self._stop_event.clear()
            self._scan_thread = threading.Thread(target=self.rescan, name="poko-indexer", daemon=True)
            self._scan_thread.start()
            return True

    def stop(self) -> None:
        self._stop_event.set()

    def join(self, timeout: float | None = None) -> None:
        if self._scan_thread and self._scan_thread.is_alive():
            self._scan_thread.join(timeout=timeout)

    def reconfigure(self, config: AppConfig, *, restart_scan: bool = True, join_timeout: float = 10.0) -> bool:
        """Apply a new config only after the current scan has fully stopped."""
        self.stop()
        self.join(timeout=join_timeout)
        if self._scan_thread and self._scan_thread.is_alive():
            self._stop_event.clear()
            raise RuntimeError("index scan did not stop in time; configuration was not changed")

        old_db_path = self.db.path
        try:
            new_db = PokoDatabase(config.library.db_path) if old_db_path != config.library.db_path else self.db
        except Exception:
            self.start_background_scan()
            raise
        self.config = config
        self._ffprobe = None
        self.db = new_db
        return self.start_background_scan() if restart_scan else False

    def status(self) -> dict[str, Any]:
        with self._scan_lock:
            data = dict(self._status)
        data["counts"] = self.db.counts_by_kind()
        data["db_path"] = str(self.db.path)
        return data

    def _set_status(self, **updates: Any) -> None:
        with self._scan_lock:
            self._status.update(updates)

    def _add_error(self, message: str) -> None:
        with self._scan_lock:
            errors = list(self._status.get("errors") or [])
            errors.append(message)
            self._status["errors"] = errors[-20:]

    def rescan(self) -> None:
        # Hold a stable config snapshot for the entire scan. reload_config() stops
        # and joins this worker before swapping configuration.
        config = self.config
        scan_id = f"{time.time():.6f}"
        ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
        ffprobe = default_ffprobe_executable(ffmpeg, config.ffmpeg.ffprobe)
        self._ffprobe = ffprobe
        scan_complete = True

        self._set_status(
            running=True,
            phase="scanning",
            current_folder=None,
            current_path=None,
            scanned_files=0,
            indexed_files=0,
            added=0,
            updated=0,
            removed=0,
            errors=[],
            started_at=time.time(),
            finished_at=None,
        )

        try:
            for folder in config.library.read_folders:
                if self._stop_event.is_set():
                    break
                self._set_status(current_folder=str(folder))
                if not folder.exists() or not folder.is_dir():
                    scan_complete = False
                    self._add_error(f"Missing folder: {folder}")
                    continue
                try:
                    paths = folder.rglob("*")
                    for path in paths:
                        if self._stop_event.is_set():
                            break
                        try:
                            if not path.is_file():
                                continue
                        except OSError as exc:
                            scan_complete = False
                            self._add_error(f"Could not inspect {path}: {exc}")
                            continue
                        with self._scan_lock:
                            self._status["scanned_files"] = int(self._status["scanned_files"]) + 1
                        kind = media_kind_for(path)
                        if kind is None:
                            continue
                        item = self._item_from_path(path, kind, ffprobe, scan_id, probe_on_scan=config.library.probe_on_scan)
                        if not item:
                            scan_complete = False
                            continue
                        try:
                            result = self.db.upsert_item(item, scan_id, enriched=config.library.probe_on_scan)
                        except Exception as exc:
                            scan_complete = False
                            self._add_error(f"Could not index {path}: {exc}")
                            continue
                        with self._scan_lock:
                            self._status["indexed_files"] = int(self._status["indexed_files"]) + 1
                            if result == "added":
                                self._status["added"] = int(self._status["added"]) + 1
                            elif result == "updated":
                                self._status["updated"] = int(self._status["updated"]) + 1
                            self._status["current_path"] = str(path)
                except OSError as exc:
                    scan_complete = False
                    self._add_error(f"Could not scan folder {folder}: {exc}")

            if self._stop_event.is_set():
                self._set_status(phase="stopped")
            elif scan_complete:
                removed = self.db.remove_missing_for_scan(scan_id)
                self._set_status(removed=removed, phase="idle")
            else:
                # Never purge rows after a partial scan. A temporarily missing
                # drive or unreadable subtree must not erase the media catalog.
                self._set_status(phase="partial")
        except Exception as exc:
            # A background worker must not die silently. Preserve the existing DB
            # and expose the failure through status instead.
            scan_complete = False
            self._add_error(f"Index scan failed: {exc}")
            self._set_status(phase="error")
        finally:
            self._set_status(running=False, current_folder=None, current_path=None, finished_at=time.time())

    def _item_from_path(
        self, path: Path, kind: str, ffprobe: str, scan_id: str, *, probe_on_scan: bool | None = None
    ) -> MediaItem | None:
        try:
            stat = path.stat()
        except OSError as exc:
            self._add_error(f"Could not stat {path}: {exc}")
            return None

        should_probe = self.config.library.probe_on_scan if probe_on_scan is None else probe_on_scan
        try:
            probe = self._probe_fn(path, ffprobe) if should_probe and kind in {"audio", "video", "image"} else {}
            meta = metadata_from_probe(path, kind, probe)
        except Exception as exc:
            self._add_error(f"Could not probe {path}: {exc}")
            return None
        if kind == "text":
            meta["title"] = self._text_title(path)
        elif not probe:
            meta["title"] = path.stem
            meta["animated"] = kind == "image" and path.suffix.lower() in {".gif", ".webp", ".apng", ".mjpg", ".mjpeg"}

        return MediaItem(
            id=self._id_for(path),
            kind=kind,
            path=path,
            title=str(meta.get("title") or path.stem),
            extension=path.suffix.lower(),
            size_bytes=stat.st_size,
            duration_s=meta.get("duration_s"),
            artist=meta.get("artist"),
            has_audio=meta.get("has_audio"),
            has_video=meta.get("has_video"),
            width=meta.get("width"),
            height=meta.get("height"),
            modified_ts=stat.st_mtime,
            metadata={k: v for k, v in meta.items() if k not in {"title", "artist"}},
        )

    @staticmethod
    def _id_for(path: Path) -> str:
        try:
            stable = os.path.normcase(str(path.resolve()))
        except OSError:
            stable = os.path.normcase(str(path.absolute()))
        return hashlib.sha1(stable.encode("utf-8", errors="ignore")).hexdigest()[:16]

    @staticmethod
    def _text_title(path: Path) -> str:
        try:
            with path.open("r", encoding="utf-8", errors="ignore") as handle:
                for line in handle:
                    stripped = line.lstrip("\ufeff").strip()
                    if stripped.startswith("#"):
                        return stripped.lstrip("#").strip() or path.stem
                    if stripped:
                        return stripped[:60]
        except OSError:
            pass
        return path.stem

    def get(self, item_id: str) -> MediaItem | None:
        item = self.db.get(item_id)
        if item and not item.path.exists():
            self.db.delete_item(item.id)
            return None
        return item

    def enrich(self, item: MediaItem) -> MediaItem:
        if item.kind not in {"audio", "video", "image"}:
            return item
        if self._has_complete_metadata(item):
            return item
        ffprobe = self._ffprobe
        if ffprobe is None:
            ffmpeg = default_ffmpeg_executable(self.config.ffmpeg.executable)
            ffprobe = default_ffprobe_executable(ffmpeg, self.config.ffmpeg.ffprobe)
            self._ffprobe = ffprobe
        try:
            probe = self._probe_fn(item.path, ffprobe)
            meta = metadata_from_probe(item.path, item.kind, probe)
        except Exception as exc:
            self._add_error(f"Could not probe {item.path}: {exc}")
            return item
        enriched = replace(
            item,
            title=str(meta.get("title") or item.title),
            artist=meta.get("artist"),
            duration_s=meta.get("duration_s"),
            has_audio=meta.get("has_audio"),
            has_video=meta.get("has_video"),
            width=meta.get("width"),
            height=meta.get("height"),
            metadata={**item.metadata, **{k: v for k, v in meta.items() if k not in {"title", "artist"}}},
        )
        self.db.update_enriched(enriched)
        return enriched

    def delete(self, item_id: str) -> None:
        self.db.delete_item(item_id)

    @staticmethod
    def is_unplayable(item: MediaItem) -> bool:
        if item.kind == "audio":
            return ("probed" in item.metadata and not item.metadata.get("probed")) or (
                bool(item.metadata.get("probed")) and (not item.has_audio or item.duration_s is None)
            )
        if item.kind == "video":
            return ("probed" in item.metadata and not item.metadata.get("probed")) or (
                bool(item.metadata.get("probed")) and (not item.has_video or item.duration_s is None)
            )
        if item.kind == "image":
            return "probed" in item.metadata and (not item.metadata["probed"] or not item.has_video or not item.width or not item.height)
        return False

    @staticmethod
    def _has_complete_metadata(item: MediaItem) -> bool:
        if item.kind == "audio":
            return item.duration_s is not None and (
                bool(item.artist) or bool(item.metadata.get("probed"))
            )
        if item.kind == "video":
            return item.duration_s is not None and item.has_audio is not None and item.width is not None
        if item.kind == "image":
            return item.width is not None or bool(item.metadata.get("probed"))
        return True

    def by_kind(self, kind: str) -> list[MediaItem]:
        return self.db.page(kind, 100000, 0)

    def page(self, kind: str, page: int | None = None, page_size: int | None = None) -> dict[str, Any]:
        selected_page = max(1, int(page or 1))
        selected_page_size = max(1, int(page_size or self.config.library.page_size))
        offset = (selected_page - 1) * selected_page_size
        items = self.db.page(kind, selected_page_size, offset)

        removed_missing = False
        for item in list(items):
            if not item.path.exists():
                self.db.delete_item(item.id)
                removed_missing = True
        if removed_missing:
            items = self.db.page(kind, selected_page_size, offset)

        total = self.db.count(kind)
        total_pages = max(1, math.ceil(total / selected_page_size))
        return {
            "kind": kind,
            "page": selected_page,
            "page_size": selected_page_size,
            "total": total,
            "total_pages": total_pages,
            "empty": total == 0,
            "indexing": self.status(),
            "items": items,
        }

    def neighbor(self, item: MediaItem, step: int) -> MediaItem | None:
        ids = self.db.ids_for_kind(item.kind)
        if not ids:
            return None
        try:
            index = ids.index(item.id)
        except ValueError:
            return None
        return self.get(ids[(index + step) % len(ids)])

    def read_text(self, item_id: str) -> dict[str, Any] | None:
        item = self.get(item_id)
        if not item or item.kind != "text":
            return None
        try:
            content = item.path.read_text(encoding="utf-8", errors="ignore").lstrip("\ufeff")
        except OSError:
            return None
        return {
            "id": item.id,
            "title": item.title,
            "format": item.extension.lstrip("."),
            "content": content,
        }

