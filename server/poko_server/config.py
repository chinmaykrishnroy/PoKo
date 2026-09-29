from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any


DEFAULT_CONFIG_PATH = Path(__file__).resolve().parents[1] / "config.yml"


def _strip_yaml_comment(raw_line: str) -> str:
    """Remove YAML comments without truncating # characters inside quotes."""
    single = False
    double = False
    escaped = False
    index = 0
    while index < len(raw_line):
        char = raw_line[index]
        if escaped:
            escaped = False
            index += 1
            continue
        if char == "\\" and double:
            escaped = True
            index += 1
            continue
        if char == '"' and not single:
            double = not double
            index += 1
            continue
        if char == "'" and not double:
            # YAML escapes a single quote inside a single-quoted scalar as ''.
            if single and index + 1 < len(raw_line) and raw_line[index + 1] == "'":
                index += 2
                continue
            single = not single
            index += 1
            continue
        if char == "#" and not single and not double:
            return raw_line[:index]
        index += 1
    return raw_line


def _parse_scalar(raw: str) -> Any:
    value = raw.strip()
    if not value:
        return ""
    if value == "[]":
        return []
    if value == "{}":
        return {}
    if value.startswith('"') and value.endswith('"'):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1].replace("''", "'")
    lower = value.lower()
    if lower in {"true", "yes", "on"}:
        return True
    if lower in {"false", "no", "off"}:
        return False
    if lower in {"null", "none", "~"}:
        return None
    try:
        if "." in value:
            return float(value)
        return int(value)
    except ValueError:
        return value


def _parse_simple_yaml(text: str) -> dict[str, Any]:
    lines: list[tuple[int, str]] = []
    for raw_line in text.splitlines():
        without_comment = _strip_yaml_comment(raw_line).rstrip()
        if not without_comment.strip():
            continue
        indent = len(without_comment) - len(without_comment.lstrip(" "))
        lines.append((indent, without_comment.strip()))

    def parse_block(index: int, indent: int) -> tuple[Any, int]:
        if index >= len(lines):
            return {}, index

        if lines[index][1].startswith("- "):
            items: list[Any] = []
            while index < len(lines) and lines[index][0] == indent and lines[index][1].startswith("- "):
                item = lines[index][1][2:].strip()
                index += 1
                if item:
                    items.append(_parse_scalar(item))
                elif index < len(lines) and lines[index][0] > indent:
                    nested, index = parse_block(index, lines[index][0])
                    items.append(nested)
                else:
                    items.append(None)
            return items, index

        mapping: dict[str, Any] = {}
        while index < len(lines) and lines[index][0] == indent and not lines[index][1].startswith("- "):
            key, sep, rest = lines[index][1].partition(":")
            if not sep:
                raise ValueError(f"Invalid config line: {lines[index][1]}")
            key = key.strip()
            rest = rest.strip()
            index += 1
            if rest:
                mapping[key] = _parse_scalar(rest)
            elif index < len(lines) and lines[index][0] > indent:
                mapping[key], index = parse_block(index, lines[index][0])
            else:
                mapping[key] = {}
        return mapping, index

    parsed, final_index = parse_block(0, lines[0][0] if lines else 0)
    if final_index != len(lines):
        raise ValueError("Could not parse entire config file")
    if not isinstance(parsed, dict):
        raise ValueError("Config root must be a mapping")
    return parsed


def load_raw_config(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    try:
        import yaml  # type: ignore

        loaded = yaml.safe_load(text) or {}
        if not isinstance(loaded, dict):
            raise ValueError("Config root must be a mapping")
        return loaded
    except ModuleNotFoundError:
        return _parse_simple_yaml(text)


def _get(raw: dict[str, Any], path: str, default: Any) -> Any:
    node: Any = raw
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            return default
        node = node[part]
    return node


def _as_bool(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "on", "enabled"}:
        return True
    if text in {"0", "false", "no", "off", "disabled", ""}:
        return False
    return default


def _as_path_list(value: Any) -> list[Path]:
    if value is None:
        return []
    if isinstance(value, (str, Path)):
        text = str(value).strip()
        return [Path(text)] if text else []
    paths: list[Path] = []
    for item in value:
        if item is None:
            continue
        text = str(item).strip()
        if text:
            paths.append(Path(text))
    return paths


@dataclass(frozen=True)
class DisplayConfig:
    width: int = 128
    height: int = 128


@dataclass(frozen=True)
class PokoDeviceConfig:
    ip: str
    base_url: str
    switch_delay_ms: int
    graphics_port: int
    audio_port: int
    video_audio_port: int
    video_frames_port: int


@dataclass(frozen=True)
class LibraryConfig:
    read_folders: list[Path]
    write_folder: Path
    page_size: int
    probe_on_scan: bool = False
    db_path: Path = Path("poko.db")


@dataclass(frozen=True)
class DefaultsConfig:
    audio_seek_seconds: int
    video_seek_seconds: int
    video_with_audio: bool
    video_aspect: str
    image_aspect: str
    image_hold_seconds: int
    motion_image_seconds: int


@dataclass(frozen=True)
class FFmpegConfig:
    executable: str
    ffprobe: str
    audio_bitrate: str
    audio_rate: int
    audio_channels: int
    sync_audio_rate: int
    sync_audio_chunk_ms: int
    video_fps: float
    video_quality: int
    graphics_fps: float
    graphics_quality: int
    start_delay_ms: int
    video_drop_late_ms: int
    highpass_enabled: bool
    highpass_hz: int
    lowpass_enabled: bool
    lowpass_hz: int


@dataclass(frozen=True)
class AppConfig:
    host: str
    port: int
    poko: PokoDeviceConfig
    library: LibraryConfig
    defaults: DefaultsConfig
    ffmpeg: FFmpegConfig
    display: DisplayConfig = DisplayConfig()

    @property
    def nexus(self) -> PokoDeviceConfig:
        return self.poko


def load_config(path: Path | str = DEFAULT_CONFIG_PATH) -> AppConfig:
    config_path = Path(path)
    raw = load_raw_config(config_path)
    db_path = Path(str(_get(raw, "library.db_path", config_path.parent / "poko.db")))
    if not db_path.is_absolute():
        db_path = config_path.parent / db_path

    poko_sec = _get(raw, "poko", {})
    if not isinstance(poko_sec, dict):
        poko_sec = {}
    nexus_sec = _get(raw, "nexus", {})
    if not isinstance(nexus_sec, dict):
        nexus_sec = {}

    poko_ip = str(poko_sec.get("ip") or nexus_sec.get("ip") or "192.168.0.4")
    poko_base_url = str(poko_sec.get("base_url") or nexus_sec.get("base_url") or f"http://{poko_ip}").rstrip("/")
    raw_delay = poko_sec.get("switch_delay_ms")
    if raw_delay is None:
        raw_delay = nexus_sec.get("switch_delay_ms")
    if raw_delay is None:
        raw_delay = 500
    poko_delay = max(0, int(raw_delay))

    poko_ports = poko_sec.get("ports") if isinstance(poko_sec.get("ports"), dict) else {}
    nexus_ports = nexus_sec.get("ports") if isinstance(nexus_sec.get("ports"), dict) else {}

    graphics_port = int(poko_ports.get("graphics") or nexus_ports.get("graphics") or 1234)
    audio_port = int(poko_ports.get("audio") or nexus_ports.get("audio") or 1235)
    video_audio_port = int(poko_ports.get("video_audio") or nexus_ports.get("video_audio") or 1236)
    video_frames_port = int(poko_ports.get("video_frames") or nexus_ports.get("video_frames") or 1237)

    disp_w = int(_get(raw, "display.width", 128))
    disp_h = int(_get(raw, "display.height", 128))
    if (disp_w, disp_h) != (128, 128):
        raise ValueError("PoKo display must be 128 x 128")
    sync_audio_rate = int(_get(raw, "ffmpeg.sync_audio_rate", 22050))
    if sync_audio_rate != 22050:
        raise ValueError("PoKo synchronized audio must be 22050 Hz")

    return AppConfig(
        host=str(_get(raw, "server.host", "0.0.0.0")),
        port=int(_get(raw, "server.port", 8765)),
        display=DisplayConfig(width=disp_w, height=disp_h),
        poko=PokoDeviceConfig(
            ip=poko_ip,
            base_url=poko_base_url,
            switch_delay_ms=poko_delay,
            graphics_port=graphics_port,
            audio_port=audio_port,
            video_audio_port=video_audio_port,
            video_frames_port=video_frames_port,
        ),
        library=LibraryConfig(
            read_folders=_as_path_list(_get(raw, "library.read_folders", [])),
            write_folder=Path(str(_get(raw, "library.write_folder", "uploads"))),
            page_size=max(1, int(_get(raw, "library.page_size", 5))),
            probe_on_scan=_as_bool(_get(raw, "library.probe_on_scan", False)),
            db_path=db_path,
        ),
        defaults=DefaultsConfig(
            audio_seek_seconds=int(_get(raw, "defaults.audio_seek_seconds", 10)),
            video_seek_seconds=int(_get(raw, "defaults.video_seek_seconds", 10)),
            video_with_audio=_as_bool(_get(raw, "defaults.video_with_audio", True), True),
            video_aspect=str(_get(raw, "defaults.video_aspect", "square")),
            image_aspect=str(_get(raw, "defaults.image_aspect", "square")),
            image_hold_seconds=int(_get(raw, "defaults.image_hold_seconds", 30)),
            motion_image_seconds=int(_get(raw, "defaults.motion_image_seconds", 8)),
        ),
        ffmpeg=FFmpegConfig(
            executable=str(_get(raw, "ffmpeg.executable", "auto")),
            ffprobe=str(_get(raw, "ffmpeg.ffprobe", "auto")),
            audio_bitrate=str(_get(raw, "ffmpeg.audio_bitrate", "96k")),
            audio_rate=int(_get(raw, "ffmpeg.audio_rate", 44100)),
            audio_channels=int(_get(raw, "ffmpeg.audio_channels", 2)),
            sync_audio_rate=sync_audio_rate,
            sync_audio_chunk_ms=int(_get(raw, "ffmpeg.sync_audio_chunk_ms", 20)),
            video_fps=max(8.0, min(20.0, float(_get(raw, "ffmpeg.video_fps", 15)))),
            video_quality=max(4, min(15, int(_get(raw, "ffmpeg.video_quality", 7)))),
            graphics_fps=max(8.0, min(24.0, float(_get(raw, "ffmpeg.graphics_fps", 18)))),
            graphics_quality=max(4, min(15, int(_get(raw, "ffmpeg.graphics_quality", 7)))),
            start_delay_ms=max(150, min(1500, int(_get(raw, "ffmpeg.start_delay_ms", 400)))),
            video_drop_late_ms=max(80, min(500, int(_get(raw, "ffmpeg.video_drop_late_ms", 160)))),
            highpass_enabled=_as_bool(_get(raw, "ffmpeg.audio_filters.highpass.enabled", False)),
            highpass_hz=max(20, min(2000, int(_get(raw, "ffmpeg.audio_filters.highpass.cutoff_hz", 80)))),
            lowpass_enabled=_as_bool(_get(raw, "ffmpeg.audio_filters.lowpass.enabled", False)),
            lowpass_hz=max(1000, min(20000, int(_get(raw, "ffmpeg.audio_filters.lowpass.cutoff_hz", 16000)))),
        ),
    )


def _yaml_string(value: Any) -> str:
    # JSON string syntax is valid YAML and safely preserves #, :, quotes and backslashes.
    return json.dumps(str(value), ensure_ascii=False)


def config_to_yaml(config: AppConfig) -> str:
    if config.library.read_folders:
        read_folders = "\n".join(f"    - {_yaml_string(folder)}" for folder in config.library.read_folders)
        read_folders_block = f"read_folders:\n{read_folders}"
    else:
        read_folders_block = "read_folders: []"
    return f"""server:
  host: {_yaml_string(config.host)}
  port: {config.port}

display:
  width: {config.display.width}
  height: {config.display.height}

poko:
  ip: {_yaml_string(config.poko.ip)}
  base_url: {_yaml_string(config.poko.base_url)}
  switch_delay_ms: {config.poko.switch_delay_ms}
  ports:
    graphics: {config.poko.graphics_port}
    audio: {config.poko.audio_port}
    video_audio: {config.poko.video_audio_port}
    video_frames: {config.poko.video_frames_port}

library:
  {read_folders_block}
  write_folder: {_yaml_string(config.library.write_folder)}
  page_size: {config.library.page_size}
  probe_on_scan: {str(config.library.probe_on_scan).lower()}
  db_path: {_yaml_string(config.library.db_path)}

defaults:
  audio_seek_seconds: {config.defaults.audio_seek_seconds}
  video_seek_seconds: {config.defaults.video_seek_seconds}
  video_with_audio: {str(config.defaults.video_with_audio).lower()}
  video_aspect: {_yaml_string(config.defaults.video_aspect)}
  image_aspect: {_yaml_string(config.defaults.image_aspect)}
  image_hold_seconds: {config.defaults.image_hold_seconds}
  motion_image_seconds: {config.defaults.motion_image_seconds}

ffmpeg:
  executable: {_yaml_string(config.ffmpeg.executable)}
  ffprobe: {_yaml_string(config.ffmpeg.ffprobe)}
  audio_bitrate: {_yaml_string(config.ffmpeg.audio_bitrate)}
  audio_rate: {config.ffmpeg.audio_rate}
  audio_channels: {config.ffmpeg.audio_channels}
  sync_audio_rate: {config.ffmpeg.sync_audio_rate}
  sync_audio_chunk_ms: {config.ffmpeg.sync_audio_chunk_ms}
  video_fps: {config.ffmpeg.video_fps:g}
  video_quality: {config.ffmpeg.video_quality}
  graphics_fps: {config.ffmpeg.graphics_fps:g}
  graphics_quality: {config.ffmpeg.graphics_quality}
  start_delay_ms: {config.ffmpeg.start_delay_ms}
  video_drop_late_ms: {config.ffmpeg.video_drop_late_ms}
  audio_filters:
    highpass:
      enabled: {str(config.ffmpeg.highpass_enabled).lower()}
      cutoff_hz: {config.ffmpeg.highpass_hz}
    lowpass:
      enabled: {str(config.ffmpeg.lowpass_enabled).lower()}
      cutoff_hz: {config.ffmpeg.lowpass_hz}
"""
