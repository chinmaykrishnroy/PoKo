from __future__ import annotations

import hashlib
import json
import os
import sqlite3
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterable, Iterator

from .models import MediaItem


class PokoDatabase:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.init_schema()

    @contextmanager
    def connect(self) -> Iterator[sqlite3.Connection]:
        conn = sqlite3.connect(self.path, timeout=30)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA synchronous=NORMAL")
        conn.execute("PRAGMA foreign_keys=ON")
        try:
            with conn:
                yield conn
        finally:
            conn.close()

    def init_schema(self) -> None:
        with self.connect() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS media_items (
                    id TEXT PRIMARY KEY,
                    kind TEXT NOT NULL,
                    path TEXT NOT NULL UNIQUE,
                    title TEXT NOT NULL,
                    extension TEXT NOT NULL,
                    size_bytes INTEGER NOT NULL,
                    duration_s REAL,
                    artist TEXT,
                    has_audio INTEGER,
                    has_video INTEGER,
                    width INTEGER,
                    height INTEGER,
                    modified_ts REAL NOT NULL,
                    metadata_json TEXT NOT NULL DEFAULT '{}',
                    discovered_ts REAL NOT NULL,
                    last_seen_scan TEXT,
                    missing INTEGER NOT NULL DEFAULT 0,
                    enriched INTEGER NOT NULL DEFAULT 0
                );

                CREATE INDEX IF NOT EXISTS idx_media_kind_title ON media_items(kind, title COLLATE NOCASE, path COLLATE NOCASE);
                CREATE INDEX IF NOT EXISTS idx_media_path ON media_items(path);
                CREATE INDEX IF NOT EXISTS idx_media_last_seen ON media_items(last_seen_scan);

                CREATE TABLE IF NOT EXISTS index_state (
                    key TEXT PRIMARY KEY,
                    value TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS request_log (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts REAL NOT NULL,
                    method TEXT NOT NULL,
                    path TEXT NOT NULL,
                    status INTEGER NOT NULL,
                    duration_ms REAL NOT NULL,
                    client TEXT,
                    error TEXT
                );

                CREATE INDEX IF NOT EXISTS idx_request_log_ts ON request_log(ts DESC);
                """
            )

    @staticmethod
    def _item_from_row(row: sqlite3.Row) -> MediaItem:
        metadata = {}
        try:
            metadata = json.loads(row["metadata_json"] or "{}")
        except json.JSONDecodeError:
            metadata = {}
        return MediaItem(
            id=row["id"],
            kind=row["kind"],
            path=Path(row["path"]),
            title=row["title"],
            extension=row["extension"],
            size_bytes=int(row["size_bytes"]),
            duration_s=row["duration_s"],
            artist=row["artist"],
            has_audio=None if row["has_audio"] is None else bool(row["has_audio"]),
            has_video=None if row["has_video"] is None else bool(row["has_video"]),
            width=row["width"],
            height=row["height"],
            modified_ts=float(row["modified_ts"]),
            metadata=metadata,
        )

    @staticmethod
    def _db_bool(value: bool | None) -> int | None:
        return None if value is None else int(bool(value))

    @staticmethod
    def _stable_id_for_path(path: str) -> str:
        candidate = Path(path)
        try:
            stable = os.path.normcase(str(candidate.resolve()))
        except OSError:
            stable = os.path.normcase(str(candidate.absolute()))
        return hashlib.sha1(stable.encode("utf-8", errors="ignore")).hexdigest()[:16]

    @classmethod
    def _available_id(cls, conn: sqlite3.Connection, path: str, *, avoid: str) -> str:
        candidate = cls._stable_id_for_path(path)
        salt = 0
        while candidate == avoid or conn.execute("SELECT 1 FROM media_items WHERE id = ?", (candidate,)).fetchone():
            salt += 1
            candidate = hashlib.sha1(f"{os.path.normcase(path)}\0{salt}".encode("utf-8", errors="ignore")).hexdigest()[:16]
        return candidate

    def upsert_item(self, item: MediaItem, scan_id: str, *, enriched: bool) -> str:
        now = time.time()
        item_path = str(item.path)
        with self.connect() as conn:
            # IDs used to be generated from lower-cased paths. On case-sensitive
            # filesystems that both collided distinct paths and means an existing
            # catalog may carry the old ID. Migrate rows transactionally before
            # the upsert so upgrades neither fail UNIQUE(path) nor lose an entry.
            path_row = conn.execute(
                "SELECT id, path, size_bytes, modified_ts, enriched FROM media_items WHERE path = ?",
                (item_path,),
            ).fetchone()
            id_row = conn.execute(
                "SELECT id, path, size_bytes, modified_ts, enriched FROM media_items WHERE id = ?",
                (item.id,),
            ).fetchone()

            previous = path_row
            if path_row is not None and path_row["id"] != item.id:
                if id_row is not None and id_row["path"] != item_path:
                    replacement = self._available_id(conn, id_row["path"], avoid=item.id)
                    conn.execute("UPDATE media_items SET id = ? WHERE id = ?", (replacement, id_row["id"]))
                conn.execute("UPDATE media_items SET id = ? WHERE id = ?", (item.id, path_row["id"]))
            elif path_row is None and id_row is not None and id_row["path"] != item_path:
                replacement = self._available_id(conn, id_row["path"], avoid=item.id)
                conn.execute("UPDATE media_items SET id = ? WHERE id = ?", (replacement, id_row["id"]))
                previous = None
            elif previous is None:
                previous = id_row

            conn.execute(
                """
                INSERT INTO media_items (
                    id, kind, path, title, extension, size_bytes, duration_s, artist,
                    has_audio, has_video, width, height, modified_ts, metadata_json,
                    discovered_ts, last_seen_scan, missing, enriched
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?)
                ON CONFLICT(id) DO UPDATE SET
                    kind = excluded.kind,
                    path = excluded.path,
                    title = excluded.title,
                    extension = excluded.extension,
                    size_bytes = excluded.size_bytes,
                    duration_s = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.duration_s ELSE media_items.duration_s END,
                    artist = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.artist ELSE media_items.artist END,
                    has_audio = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.has_audio ELSE media_items.has_audio END,
                    has_video = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.has_video ELSE media_items.has_video END,
                    width = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.width ELSE media_items.width END,
                    height = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.height ELSE media_items.height END,
                    modified_ts = excluded.modified_ts,
                    metadata_json = CASE WHEN excluded.enriched = 1 OR excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN excluded.metadata_json ELSE media_items.metadata_json END,
                    last_seen_scan = excluded.last_seen_scan,
                    missing = 0,
                    enriched = CASE WHEN excluded.enriched = 1 THEN 1 WHEN excluded.size_bytes != media_items.size_bytes OR excluded.modified_ts != media_items.modified_ts THEN 0 ELSE media_items.enriched END
                """,
                (
                    item.id,
                    item.kind,
                    item_path,
                    item.title,
                    item.extension,
                    item.size_bytes,
                    item.duration_s,
                    item.artist,
                    self._db_bool(item.has_audio),
                    self._db_bool(item.has_video),
                    item.width,
                    item.height,
                    item.modified_ts,
                    json.dumps(item.metadata, ensure_ascii=False),
                    now,
                    scan_id,
                    int(enriched),
                ),
            )
        if previous is None:
            return "added"
        if int(previous["size_bytes"]) != item.size_bytes or float(previous["modified_ts"]) != item.modified_ts:
            return "updated"
        return "seen"

    def update_enriched(self, item: MediaItem) -> None:
        with self.connect() as conn:
            conn.execute(
                """
                UPDATE media_items
                SET title = ?, duration_s = ?, artist = ?, has_audio = ?, has_video = ?,
                    width = ?, height = ?, metadata_json = ?, enriched = 1
                WHERE id = ?
                """,
                (
                    item.title,
                    item.duration_s,
                    item.artist,
                    self._db_bool(item.has_audio),
                    self._db_bool(item.has_video),
                    item.width,
                    item.height,
                    json.dumps(item.metadata, ensure_ascii=False),
                    item.id,
                ),
            )

    def delete_item(self, item_id: str) -> None:
        with self.connect() as conn:
            conn.execute("DELETE FROM media_items WHERE id = ?", (item_id,))

    def remove_missing_for_scan(self, scan_id: str) -> int:
        with self.connect() as conn:
            result = conn.execute(
                "DELETE FROM media_items WHERE COALESCE(last_seen_scan, '') != ?",
                (scan_id,),
            )
            return int(result.rowcount or 0)

    def get(self, item_id: str) -> MediaItem | None:
        with self.connect() as conn:
            row = conn.execute("SELECT * FROM media_items WHERE id = ?", (item_id,)).fetchone()
        return self._item_from_row(row) if row else None

    def count(self, kind: str | None = None) -> int:
        with self.connect() as conn:
            if kind:
                row = conn.execute("SELECT COUNT(*) AS c FROM media_items WHERE kind = ?", (kind,)).fetchone()
            else:
                row = conn.execute("SELECT COUNT(*) AS c FROM media_items").fetchone()
        return int(row["c"] if row else 0)

    def counts_by_kind(self) -> dict[str, int]:
        counts = {"audio": 0, "video": 0, "image": 0, "text": 0}
        with self.connect() as conn:
            rows = conn.execute("SELECT kind, COUNT(*) AS c FROM media_items GROUP BY kind").fetchall()
        for row in rows:
            counts[str(row["kind"])] = int(row["c"])
        return counts

    def page(self, kind: str, limit: int, offset: int) -> list[MediaItem]:
        with self.connect() as conn:
            rows = conn.execute(
                """
                SELECT * FROM media_items
                WHERE kind = ?
                ORDER BY title COLLATE NOCASE, path COLLATE NOCASE
                LIMIT ? OFFSET ?
                """,
                (kind, limit, offset),
            ).fetchall()
        return [self._item_from_row(row) for row in rows]

    def ids_for_kind(self, kind: str) -> list[str]:
        with self.connect() as conn:
            rows = conn.execute(
                "SELECT id FROM media_items WHERE kind = ? ORDER BY title COLLATE NOCASE, path COLLATE NOCASE",
                (kind,),
            ).fetchall()
        return [str(row["id"]) for row in rows]

    def recent_items(self, limit: int = 20) -> list[dict[str, Any]]:
        with self.connect() as conn:
            rows = conn.execute(
                """
                SELECT id, kind, title, path, discovered_ts, enriched
                FROM media_items
                ORDER BY discovered_ts DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [dict(row) for row in rows]

    def log_request(self, method: str, path: str, status: int, duration_ms: float, client: str | None, error: str | None = None) -> None:
        with self.connect() as conn:
            conn.execute(
                """
                INSERT INTO request_log (ts, method, path, status, duration_ms, client, error)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (time.time(), method, path, status, duration_ms, client, error),
            )
            conn.execute(
                """
                DELETE FROM request_log
                WHERE id NOT IN (SELECT id FROM request_log ORDER BY ts DESC LIMIT 500)
                """
            )

    def recent_requests(self, limit: int = 80) -> list[dict[str, Any]]:
        with self.connect() as conn:
            rows = conn.execute(
                """
                SELECT id, ts, method, path, status, duration_ms, client, error
                FROM request_log
                ORDER BY ts DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [dict(row) for row in rows]

    def set_state(self, key: str, value: Any) -> None:
        with self.connect() as conn:
            conn.execute(
                """
                INSERT INTO index_state (key, value) VALUES (?, ?)
                ON CONFLICT(key) DO UPDATE SET value = excluded.value
                """,
                (key, json.dumps(value, ensure_ascii=False)),
            )

    def get_state(self, key: str, default: Any = None) -> Any:
        with self.connect() as conn:
            row = conn.execute("SELECT value FROM index_state WHERE key = ?", (key,)).fetchone()
        if not row:
            return default
        try:
            return json.loads(row["value"])
        except json.JSONDecodeError:
            return default

