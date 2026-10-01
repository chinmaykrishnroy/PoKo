from __future__ import annotations

import base64
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any

from .config import AppConfig
from .models import Icon, MediaItem


AUDIO_EXTS = {".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".wma"}
VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v", ".ts", ".mpg", ".mpeg"}
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".apng", ".mjpg", ".mjpeg"}
TEXT_EXTS = {".txt", ".md", ".markdown"}
MOTION_IMAGE_EXTS = {".gif", ".webp", ".apng", ".mjpg", ".mjpeg"}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_ffmpeg_executable(configured: str = "auto") -> str:
    if configured and configured != "auto":
        return configured
    local = repo_root() / "tools" / "ffmpeg" / "ffmpeg-8.1.1-essentials_build" / "bin" / "ffmpeg.exe"
    if local.exists():
        return str(local)
    winget = Path(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Links\ffmpeg.exe"))
    if winget.exists():
        return str(winget)
    return shutil.which("ffmpeg") or "ffmpeg"


def default_ffprobe_executable(ffmpeg_executable: str, configured: str = "auto") -> str:
    if configured and configured != "auto":
        return configured
    ffmpeg_path = Path(ffmpeg_executable)
    sibling = ffmpeg_path.with_name("ffprobe.exe" if ffmpeg_path.suffix.lower() == ".exe" else "ffprobe")
    if sibling.exists():
        return str(sibling)
    winget = Path(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Links\ffprobe.exe"))
    if winget.exists():
        return str(winget)
    return shutil.which("ffprobe") or "ffprobe"


def media_kind_for(path: Path) -> str | None:
    ext = path.suffix.lower()
    if ext in AUDIO_EXTS:
        return "audio"
    if ext in VIDEO_EXTS:
        return "video"
    if ext in IMAGE_EXTS:
        return "image"
    if ext in TEXT_EXTS:
        return "text"
    return None


def video_filter(aspect: str, fps: float | None = None, width: int = 128, height: int = 128) -> str:
    if aspect == "fit":
        base = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
    else:
        base = f"scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
    if fps is not None:
        return f"{base},fps={fps:g}"
    return base


def thumbnail_filter(size: int = 28) -> str:
    selected = max(16, min(int(size), 128))
    return (
        f"scale={selected}:{selected}:force_original_aspect_ratio=decrease,"
        f"pad={selected}:{selected}:(ow-iw)/2:(oh-ih)/2:color=black"
    )


def audio_filter(config: AppConfig) -> str | None:
    filters: list[str] = []
    if config.ffmpeg.highpass_enabled:
        filters.append(f"highpass=f={config.ffmpeg.highpass_hz}")
    if config.ffmpeg.lowpass_enabled:
        filters.append(f"lowpass=f={config.ffmpeg.lowpass_hz}")
    return ",".join(filters) or None


def _run_json_command(cmd: list[str], timeout: float = 15) -> dict[str, Any]:
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return {}
    if result.returncode != 0:
        return {}
    stdout = result.stdout.decode("utf-8", errors="replace") if isinstance(result.stdout, bytes) else result.stdout
    try:
        return json.loads(stdout or "{}")
    except json.JSONDecodeError:
        return {}


def ffprobe_media(path: Path, ffprobe: str) -> dict[str, Any]:
    return _run_json_command(
        [
            ffprobe,
            "-v",
            "error",
            "-show_format",
            "-show_streams",
            "-print_format",
            "json",
            str(path),
        ]
    )


def metadata_from_probe(path: Path, kind: str, probe: dict[str, Any]) -> dict[str, Any]:
    streams = probe.get("streams") if isinstance(probe.get("streams"), list) else []
    fmt = probe.get("format") if isinstance(probe.get("format"), dict) else {}
    tags: dict[str, Any] = {}
    if isinstance(fmt.get("tags"), dict):
        tags.update(fmt["tags"])
    for stream in streams:
        if isinstance(stream.get("tags"), dict):
            for key, value in stream["tags"].items():
                tags.setdefault(key, value)
    tags_l = {str(k).lower(): str(v) for k, v in tags.items()}
    duration = None
    try:
        duration = float(fmt.get("duration"))
    except (TypeError, ValueError):
        pass
    if duration is None:
        for stream in streams:
            try:
                duration = float(stream.get("duration"))
                break
            except (TypeError, ValueError):
                continue

    audio_streams = [s for s in streams if s.get("codec_type") == "audio"]
    video_streams = [s for s in streams if s.get("codec_type") == "video"]
    first_video = video_streams[0] if video_streams else {}
    artist = (
        tags_l.get("artist")
        or tags_l.get("album_artist")
        or tags_l.get("albumartist")
        or tags_l.get("artists")
        or tags_l.get("performer")
        or tags_l.get("composer")
    )

    # Container/audio-stream titles are useful for songs, but video files often
    # carry generic audio track labels such as "Stereo" or "English". Keep the
    # user-visible video/image name tied to the file instead of leaking a stream
    # label into the media catalog.
    title = tags_l.get("title") or path.stem if kind == "audio" else path.stem

    return {
        "title": title,
        "artist": artist,
        "duration_s": duration,
        "has_audio": bool(audio_streams),
        "has_video": bool(video_streams),
        "width": int(first_video.get("width")) if first_video.get("width") else None,
        "height": int(first_video.get("height")) if first_video.get("height") else None,
        "animated": kind == "image" and path.suffix.lower() in MOTION_IMAGE_EXTS,
        "probed": bool(probe),
    }


def audio_tcp_command(
    path: Path,
    config: AppConfig,
    start_s: float = 0,
    *,
    target_host: str | None = None,
) -> list[str]:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-re"]
    if start_s > 0:
        cmd += ["-ss", f"{start_s:.3f}"]
    cmd += [
        "-i",
        str(path),
        "-vn",
    ]
    selected_filter = audio_filter(config)
    if selected_filter:
        cmd += ["-af", selected_filter]
    cmd += [
        "-c:a",
        "libmp3lame",
        "-b:a",
        config.ffmpeg.audio_bitrate,
        "-ar",
        str(config.ffmpeg.audio_rate),
        "-ac",
        str(config.ffmpeg.audio_channels),
        "-f",
        "mp3",
        f"tcp://{target_host or config.poko.ip}:{config.poko.audio_port}?tcp_nodelay=1",
    ]
    return cmd


def graphics_video_tcp_command(
    path: Path,
    config: AppConfig,
    *,
    start_s: float = 0,
    aspect: str | None = None,
    duration_s: float | None = None,
    image_loop: bool = False,
    stream_loop: bool = False,
    fps: float | None = None,
    quality: int | None = None,
) -> list[str]:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    selected_aspect = aspect or config.defaults.video_aspect
    selected_fps = fps if fps is not None else config.ffmpeg.graphics_fps
    selected_quality = quality if quality is not None else config.ffmpeg.graphics_quality
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-re"]
    if image_loop:
        cmd += ["-loop", "1"]
    if stream_loop:
        cmd += ["-stream_loop", "-1"]
    if start_s > 0:
        cmd += ["-ss", f"{start_s:.3f}"]
    if duration_s is not None and duration_s > 0:
        cmd += ["-t", f"{duration_s:.3f}"]
    cmd += [
        "-i",
        str(path),
        "-map",
        "0:v:0",
        "-vf",
        video_filter(selected_aspect, selected_fps, config.display.width, config.display.height),
        "-an",
        "-c:v",
        "mjpeg",
        "-pix_fmt",
        "yuvj420p",
        "-q:v",
        str(selected_quality),
        "-f",
        "mjpeg",
        f"tcp://{config.poko.ip}:{config.poko.graphics_port}?tcp_nodelay=1",
    ]
    return cmd


def synced_audio_pipe_command(path: Path, config: AppConfig, start_s: float = 0, duration_s: float | None = None) -> list[str]:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error"]
    if start_s > 0:
        cmd += ["-ss", f"{start_s:.3f}"]
    if duration_s is not None and duration_s > 0:
        cmd += ["-t", f"{duration_s:.3f}"]
    cmd += [
        "-i",
        str(path),
        "-map",
        "0:a:0",
        "-vn",
    ]
    selected_filter = audio_filter(config)
    if selected_filter:
        cmd += ["-af", selected_filter]
    cmd += [
        "-ac",
        "1",
        "-ar",
        str(config.ffmpeg.sync_audio_rate),
        "-c:a",
        "pcm_s16le",
        "-f",
        "s16le",
        "-",
    ]
    return cmd


def synced_video_pipe_command(
    path: Path,
    config: AppConfig,
    *,
    start_s: float = 0,
    duration_s: float | None = None,
    aspect: str | None = None,
    fps: float | None = None,
    quality: int | None = None,
) -> list[str]:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error"]
    if start_s > 0:
        cmd += ["-ss", f"{start_s:.3f}"]
    if duration_s is not None and duration_s > 0:
        cmd += ["-t", f"{duration_s:.3f}"]
    cmd += [
        "-i",
        str(path),
        "-map",
        "0:v:0",
        "-vf",
        video_filter(
            aspect or config.defaults.video_aspect,
            fps if fps is not None else config.ffmpeg.video_fps,
            config.display.width,
            config.display.height,
        ),
        "-an",
        "-c:v",
        "mjpeg",
        "-pix_fmt",
        "yuvj420p",
        "-q:v",
        str(quality if quality is not None else config.ffmpeg.video_quality),
        "-f",
        "image2pipe",
        "-",
    ]
    return cmd


def _generic_svg(kind: str, size: int = 28) -> str:
    selected = max(16, min(int(size), 128))
    palette = {
        "audio": ("#0ea5e9", "A"),
        "video": ("#14b8a6", "V"),
        "image": ("#a855f7", "I"),
        "text": ("#f59e0b", "T"),
    }
    color, label = palette.get(kind, ("#64748b", "?"))
    return (
        f"<svg xmlns='http://www.w3.org/2000/svg' width='{selected}' height='{selected}' viewBox='0 0 {selected} {selected}'>"
        f"<rect width='{selected}' height='{selected}' rx='{max(4, selected // 7)}' fill='{color}'/>"
        f"<text x='{selected / 2}' y='{selected * 0.68:.1f}' fill='white' font-size='{selected * 0.5:.1f}' text-anchor='middle' "
        "font-family='Arial, sans-serif' font-weight='700'>"
        f"{label}</text></svg>"
    )


def generic_icon(kind: str, size: int = 28) -> Icon:
    selected = max(16, min(int(size), 128))
    data = base64.b64encode(_generic_svg(kind, selected).encode("utf-8")).decode("ascii")
    return Icon(mime="image/svg+xml", width=selected, height=selected, data=data, source="generic")


def _thumbnail_with_python_ffmpeg(path: Path, ffmpeg_path: str, timeout: float, size: int, kind: str) -> bytes | None:
    try:
        from ffmpeg import FFmpeg  # type: ignore
    except (ModuleNotFoundError, ImportError):
        return None

    try:
        try:
            ffmpeg = FFmpeg(executable=ffmpeg_path)
        except TypeError:
            ffmpeg = FFmpeg()
        command = ffmpeg.option("hide_banner").option("loglevel", "error")
        if kind in {"audio", "image"}:
            command = command.input(str(path))
        else:
            command = command.input(str(path), ss="1")
        return (
            command
            .output(
                "pipe:1",
                {"map": "0:v:0?", "frames:v": "1", "codec:v": "mjpeg", "q:v": "6"},
                vf=thumbnail_filter(size) + ",format=yuvj420p",
                f="image2pipe",
            )
            .execute(timeout=timeout)
        )
    except Exception:
        return None


def thumbnail_icon(path: Path, kind: str, config: AppConfig, timeout: float = 5, size: int = 28) -> Icon:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    selected = max(16, min(int(size), 128))
    payload = _thumbnail_with_python_ffmpeg(path, ffmpeg, timeout, selected, kind)
    if not payload or len(payload) > 32000:
        seek_points = [None] if kind == "audio" else ["1", "0.05"]
        for seek in seek_points:
            cmd = [ffmpeg, "-hide_banner", "-loglevel", "error"]
            if seek is not None:
                cmd += ["-ss", seek]
            cmd += [
                "-i", str(path),
                "-map_metadata", "-1",
                "-map", "0:v:0?",
                "-frames:v", "1",
                "-vf", thumbnail_filter(selected) + ",format=yuvj420p",
                "-f", "image2pipe",
                "-vcodec", "mjpeg",
                "-q:v", "6",
                "-",
            ]
            try:
                result = subprocess.run(cmd, capture_output=True, timeout=timeout, check=False)
                payload = result.stdout if result.returncode == 0 else b""
            except (OSError, subprocess.TimeoutExpired):
                payload = b""
            if payload and len(payload) <= 32000:
                break

    if not payload:
        return generic_icon(kind, selected)
    return Icon(
        mime="image/jpeg",
        width=selected,
        height=selected,
        data=base64.b64encode(payload).decode("ascii"),
        source="embedded" if kind == "audio" else "thumbnail",
    )


def thumbnail_raw_jpeg(path: Path, kind: str, config: AppConfig, timeout: float = 6, size: int = 80) -> bytes | None:
    ffmpeg = default_ffmpeg_executable(config.ffmpeg.executable)
    selected = max(16, min(int(size), 128))
    seek_points = [None] if kind in {"audio", "image"} else ["1", "0.05", "0"]
    for seek in seek_points:
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "error"]
        if seek is not None:
            cmd += ["-ss", seek]
        cmd += [
            "-i", str(path),
            "-map_metadata", "-1",
            "-map", "0:v:0?",
            "-frames:v", "1",
            "-vf", thumbnail_filter(selected) + ",format=yuvj420p",
            "-f", "image2pipe",
            "-vcodec", "mjpeg",
            "-q:v", "6",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=timeout, check=False)
            if result.returncode == 0 and result.stdout and len(result.stdout) > 50:
                return result.stdout
        except (OSError, subprocess.TimeoutExpired):
            pass
    return None


def command_preview(cmd: list[str]) -> str:
    return " ".join(f'"{part}"' if " " in part else part for part in cmd)

