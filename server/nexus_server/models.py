from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class Icon:
    mime: str
    width: int
    height: int
    data: str
    source: str = "generic"

    def to_json(self) -> dict[str, Any]:
        return {
            "mime": self.mime,
            "width": self.width,
            "height": self.height,
            "data": self.data,
            "source": self.source,
        }


@dataclass(frozen=True)
class MediaItem:
    id: str
    kind: str
    path: Path
    title: str
    extension: str
    size_bytes: int
    duration_s: float | None = None
    artist: str | None = None
    has_audio: bool | None = None
    has_video: bool | None = None
    width: int | None = None
    height: int | None = None
    modified_ts: float = 0
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_json(self, icon: Icon | None = None) -> dict[str, Any]:
        data: dict[str, Any] = {
            "id": self.id,
            "kind": self.kind,
            "title": self.title,
            "extension": self.extension,
            "size_bytes": self.size_bytes,
            "duration_s": self.duration_s,
        }
        if self.kind == "audio":
            data["artist"] = self.artist or "Unknown Artist"
        if self.kind == "video":
            data["has_audio"] = bool(self.has_audio)
            data["width"] = self.width
            data["height"] = self.height
        if self.kind == "image":
            data["width"] = self.width
            data["height"] = self.height
            data["animated"] = bool(self.metadata.get("animated"))
            data["motion_duration_ms"] = round(self.duration_s * 1000) if self.duration_s else None
        if self.kind == "text":
            data["format"] = self.extension.lstrip(".").lower()
        if icon is not None:
            data["icon"] = icon.to_json()
        return data
