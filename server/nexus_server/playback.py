from __future__ import annotations

import socket
import struct
import subprocess
import threading
import time
import queue
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from .config import AppConfig
from .device import DeviceClient
from .ffmpeg_tools import (
    command_preview,
    audio_tcp_command,
    graphics_video_tcp_command,
    synced_audio_pipe_command,
    synced_video_pipe_command,
)
from .media_index import MediaIndex
from .models import MediaItem


MAGIC = b"NAV1"
PACKET_AUDIO = 1
PACKET_VIDEO = 2


def resolve_video_tuning(
    config: AppConfig,
    *,
    synced: bool,
    profile: str | None = None,
    fps: float | None = None,
    jpeg_quality: int | None = None,
) -> tuple[str, float, int]:
    selected_profile = (profile or "balanced").lower()
    if selected_profile not in {"balanced", "quality", "smooth"}:
        selected_profile = "balanced"
    base_fps = config.ffmpeg.video_fps if synced else config.ffmpeg.graphics_fps
    base_quality = config.ffmpeg.video_quality if synced else config.ffmpeg.graphics_quality
    if selected_profile == "quality":
        base_quality = max(4, base_quality - 2)
    elif selected_profile == "smooth":
        base_fps += 3
        base_quality = min(15, base_quality + 2)
    max_fps = 20.0 if synced else 24.0
    return (
        selected_profile,
        max(8.0, min(max_fps, float(fps if fps is not None else base_fps))),
        max(4, min(15, int(jpeg_quality if jpeg_quality is not None else base_quality))),
    )


@dataclass
class PlaybackState:
    active: bool = False
    mode: str = "idle"
    item_id: str | None = None
    kind: str | None = None
    title: str | None = None
    path: str | None = None
    started_at: float | None = None
    start_s: float = 0
    aspect: str | None = None
    with_audio: bool | None = None
    command: list[str] | None = None
    counters: dict[str, Any] = field(default_factory=dict)

    def position_s(self) -> float:
        if not self.active or self.started_at is None:
            return self.start_s
        return self.start_s + max(0, time.monotonic() - self.started_at)

    def to_json(self) -> dict[str, Any]:
        return {
            "active": self.active,
            "mode": self.mode,
            "item_id": self.item_id,
            "kind": self.kind,
            "title": self.title,
            "path": self.path,
            "start_s": round(self.start_s, 3),
            "position_s": round(self.position_s(), 3),
            "aspect": self.aspect,
            "with_audio": self.with_audio,
            "command": self.command,
            "command_preview": command_preview(self.command) if self.command else None,
            "counters": dict(self.counters),
        }


class ProcessHandle:
    def __init__(self, command: list[str], dry_run: bool = False) -> None:
        self.command = command
        self.dry_run = dry_run
        self.process: subprocess.Popen | None = None
        if not dry_run:
            self.process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def stop(self) -> None:
        if not self.process:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.process = None


class SyncedAVStreamer:
    def __init__(
        self,
        item: MediaItem,
        config: AppConfig,
        *,
        start_s: float = 0,
        aspect: str | None = None,
        profile: str | None = None,
        fps: float | None = None,
        jpeg_quality: int | None = None,
        dry_run: bool = False,
    ) -> None:
        self.item = item
        self.config = config
        self.start_s = start_s
        self.aspect = aspect or config.defaults.video_aspect
        self.profile, self.video_fps, self.video_quality = resolve_video_tuning(
            config, synced=True, profile=profile, fps=fps, jpeg_quality=jpeg_quality
        )
        self.dry_run = dry_run
        self.stop_event = threading.Event()
        self.threads: list[threading.Thread] = []
        self.processes: list[subprocess.Popen] = []
        self.lock = threading.Lock()
        self.wall_start = 0.0
        self.audio_ready = threading.Event()
        self.video_ready = threading.Event()
        self.release_senders = threading.Event()
        self.counters: dict[str, Any] = {
            "audio_packets": 0,
            "video_frames": 0,
            "video_dropped_sender": 0,
            "audio_connected": False,
            "video_connected": False,
            "startup_ready": False,
            "audio_error": None,
            "video_error": None,
            "profile": self.profile,
            "fps": self.video_fps,
            "jpeg_quality": self.video_quality,
        }

    @property
    def commands(self) -> list[list[str]]:
        return [
            synced_audio_pipe_command(self.item.path, self.config, self.start_s),
            synced_video_pipe_command(
                self.item.path,
                self.config,
                start_s=self.start_s,
                aspect=self.aspect,
                fps=self.video_fps,
                quality=self.video_quality,
            ),
        ]

    def start(self, timeout_s: float = 3.5) -> bool:
        if self.dry_run:
            self.counters["startup_ready"] = True
            return True
        self.threads = [
            threading.Thread(target=self._audio_sender, name="poko-sync-audio", daemon=True),
            threading.Thread(target=self._video_sender, name="poko-sync-video", daemon=True),
        ]
        for thread in self.threads:
            thread.start()

        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.audio_ready.is_set() and self.video_ready.is_set():
                break
            time.sleep(0.01)

        with self.lock:
            ready = bool(
                self.counters["audio_connected"]
                and self.counters["video_connected"]
                and not self.counters["audio_error"]
                and not self.counters["video_error"]
            )
            self.counters["startup_ready"] = ready
        if ready:
            self.wall_start = time.monotonic() + self.config.ffmpeg.start_delay_ms / 1000
            self.release_senders.set()
            return True

        self.stop_event.set()
        self.release_senders.set()
        return False

    def stop(self) -> None:
        self.stop_event.set()
        self.release_senders.set()
        for proc in list(self.processes):
            if proc.poll() is None:
                proc.terminate()
        for thread in self.threads:
            thread.join(timeout=1.5)
        for proc in list(self.processes):
            if proc.poll() is None:
                proc.kill()

    def _popen(self, command: list[str]) -> subprocess.Popen:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        with self.lock:
            self.processes.append(proc)
        return proc

    def _set_sender_error(self, channel: str, error: BaseException | str) -> None:
        if self.stop_event.is_set():
            return
        message = str(error).strip() or type(error).__name__
        with self.lock:
            self.counters[f"{channel}_error"] = message[:300]

    def _connect(self, port: int) -> socket.socket:
        deadline = time.monotonic() + 3.0
        last_error: OSError | None = None
        while not self.stop_event.is_set() and time.monotonic() < deadline:
            try:
                sock = socket.create_connection((self.config.nexus.ip, port), timeout=1.0)
                sock.settimeout(None)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                return sock
            except OSError as exc:
                last_error = exc
                time.sleep(0.08)
        raise ConnectionError(f"cannot connect to {self.config.nexus.ip}:{port}: {last_error}")

    @staticmethod
    def _send_packet(sock: socket.socket, packet_type: int, timestamp_ms: int, payload: bytes) -> None:
        header = struct.pack("<4sB3xII", MAGIC, packet_type, timestamp_ms, len(payload))
        sock.sendall(header)
        sock.sendall(payload)

    def _audio_sender(self) -> None:
        rate = self.config.ffmpeg.sync_audio_rate
        chunk_samples = max(64, int(rate * self.config.ffmpeg.sync_audio_chunk_ms / 1000))
        chunk_bytes = chunk_samples * 2
        proc: subprocess.Popen | None = None
        samples_sent = 0

        try:
            with self._connect(self.config.nexus.video_audio_port) as sock:
                proc = self._popen(synced_audio_pipe_command(self.item.path, self.config, self.start_s))
                with self.lock:
                    self.counters["audio_connected"] = True
                self.audio_ready.set()
                if not self.release_senders.wait(timeout=3.0) or self.stop_event.is_set():
                    return
                while not self.stop_event.is_set():
                    data = proc.stdout.read(chunk_bytes) if proc.stdout else b""
                    if not data:
                        if samples_sent == 0:
                            self._set_sender_error("audio", "FFmpeg produced no audio data")
                        break
                    timestamp_ms = int(round((samples_sent * 1000.0) / rate))
                    samples_sent += len(data) // 2
                    delay = self.wall_start + timestamp_ms / 1000.0 - time.monotonic()
                    if delay > 0:
                        time.sleep(delay)
                    self._send_packet(sock, PACKET_AUDIO, timestamp_ms, data)
                    with self.lock:
                        self.counters["audio_packets"] += 1
        except (OSError, subprocess.SubprocessError) as exc:
            self._set_sender_error("audio", exc)
        finally:
            self.audio_ready.set()
            with self.lock:
                self.counters["audio_connected"] = False
            if proc is not None and proc.poll() is None:
                proc.terminate()

    def _video_sender(self) -> None:
        fps = self.video_fps
        proc: subprocess.Popen | None = None
        buffer = bytearray()
        frame_index = 0

        try:
            with self._connect(self.config.nexus.video_frames_port) as sock:
                proc = self._popen(
                    synced_video_pipe_command(
                        self.item.path,
                        self.config,
                        start_s=self.start_s,
                        aspect=self.aspect,
                        fps=self.video_fps,
                        quality=self.video_quality,
                    )
                )
                with self.lock:
                    self.counters["video_connected"] = True
                self.video_ready.set()
                if not self.release_senders.wait(timeout=3.0) or self.stop_event.is_set():
                    return

                while not self.stop_event.is_set():
                    chunk = proc.stdout.read(4096) if proc.stdout else b""
                    if not chunk:
                        if frame_index == 0:
                            self._set_sender_error("video", "FFmpeg produced no video frames")
                        break
                    buffer.extend(chunk)

                    while True:
                        start = buffer.find(b"\xff\xd8")
                        if start < 0:
                            if len(buffer) > 2:
                                del buffer[:-2]
                            break
                        end = buffer.find(b"\xff\xd9", start + 2)
                        if end < 0:
                            if start > 0:
                                del buffer[:start]
                            break

                        jpg = bytes(buffer[start : end + 2])
                        del buffer[: end + 2]
                        timestamp_ms = int(round((frame_index * 1000.0) / fps))
                        frame_index += 1

                        target_time = self.wall_start + timestamp_ms / 1000.0
                        now = time.monotonic()
                        lateness_ms = int((now - target_time) * 1000)
                        if lateness_ms > self.config.ffmpeg.video_drop_late_ms:
                            with self.lock:
                                self.counters["video_dropped_sender"] += 1
                            continue
                        delay = target_time - now
                        if delay > 0:
                            time.sleep(delay)
                        self._send_packet(sock, PACKET_VIDEO, timestamp_ms, jpg)
                        with self.lock:
                            self.counters["video_frames"] += 1
        except (OSError, subprocess.SubprocessError) as exc:
            self._set_sender_error("video", exc)
        finally:
            self.video_ready.set()
            with self.lock:
                self.counters["video_connected"] = False
            if proc is not None and proc.poll() is None:
                proc.terminate()


class PlaybackManager:
    def __init__(self, config: AppConfig, index: MediaIndex, device: DeviceClient, *, dry_run: bool = False) -> None:
        self.config = config
        self.index = index
        self.device = device
        self.dry_run = dry_run
        self._lock = threading.RLock()
        self._process: ProcessHandle | None = None
        self._streamer: SyncedAVStreamer | None = None
        self._state = PlaybackState()
        self._operation_lock = threading.Lock()
        self._operation_queue: queue.Queue[tuple[int, Callable[[], dict[str, Any]]]] = queue.Queue()
        self._operations: dict[int, dict[str, Any]] = {}
        self._operation_keys: dict[str, int] = {}
        self._next_operation_id = 1
        self._operation_worker = threading.Thread(
            target=self._run_operations,
            name="poko-playback-worker",
            daemon=True,
        )
        self._operation_worker.start()

    def _run_operations(self) -> None:
        while True:
            operation_id, action = self._operation_queue.get()
            with self._operation_lock:
                operation = self._operations.get(operation_id)
                if operation is not None:
                    operation["status"] = "running"
                    operation["started_at"] = time.time()
            try:
                result = action()
            except Exception as exc:  # Keep the worker alive and expose the failure to the device.
                result = {"ok": False, "error": f"playback operation failed: {exc}"}
            with self._operation_lock:
                operation = self._operations.get(operation_id)
                if operation is not None:
                    succeeded = bool(result.get("ok"))
                    operation["ok"] = succeeded
                    operation["status"] = "succeeded" if succeeded else "failed"
                    operation["finished_at"] = time.time()
                    operation["result"] = result
                self._prune_operations_locked()
            self._operation_queue.task_done()

    def _prune_operations_locked(self) -> None:
        if len(self._operations) <= 64:
            return
        for operation_id in sorted(self._operations):
            if len(self._operations) <= 48:
                break
            if self._operations[operation_id]["status"] in {"succeeded", "failed"}:
                operation = self._operations.pop(operation_id)
                request_id = operation.get("request_id")
                if request_id and self._operation_keys.get(request_id) == operation_id:
                    del self._operation_keys[request_id]

    def _submit_operation(
        self,
        name: str,
        action: Callable[[], dict[str, Any]],
        *,
        mode: str | None = None,
        request_id: str | None = None,
    ) -> dict[str, Any]:
        selected_request_id = str(request_id or "").strip()[:96]
        with self._operation_lock:
            if selected_request_id:
                existing_id = self._operation_keys.get(selected_request_id)
                existing = self._operations.get(existing_id) if existing_id is not None else None
                if existing is not None:
                    response = dict(existing)
                    response["duplicate"] = True
                    return response
                self._operation_keys.pop(selected_request_id, None)

            operation_id = self._next_operation_id
            self._next_operation_id += 1
            operation = {
                "ok": True,
                "accepted": True,
                "operation_id": operation_id,
                "operation": name,
                "status": "queued",
                "queued_at": time.time(),
            }
            if selected_request_id:
                operation["request_id"] = selected_request_id
            if mode:
                operation["mode"] = mode
            self._operations[operation_id] = operation
            if selected_request_id:
                self._operation_keys[selected_request_id] = operation_id
            response = dict(operation)
        self._operation_queue.put((operation_id, action))
        return response

    def operation_status(self, operation_id: int) -> dict[str, Any] | None:
        with self._operation_lock:
            operation = self._operations.get(operation_id)
            return dict(operation) if operation is not None else None

    def submit_video(
        self,
        item: MediaItem,
        *,
        audio: str | bool | None = None,
        aspect: str | None = None,
        start_s: float = 0,
        switch_device: bool = True,
        profile: str | None = None,
        fps: float | None = None,
        jpeg_quality: int | None = None,
        request_id: str | None = None,
    ) -> dict[str, Any]:
        use_sync = bool(self._resolve_audio_request(audio) and item.has_audio)
        mode = "video_player" if use_sync else "graphics_player"
        return self._submit_operation(
            "play_video",
            lambda: self.play_video(
                item,
                audio=audio,
                aspect=aspect,
                start_s=start_s,
                switch_device=switch_device,
                profile=profile,
                fps=fps,
                jpeg_quality=jpeg_quality,
            ),
            mode=mode,
            request_id=request_id,
        )

    def submit_seek(
        self,
        seconds: float | None = None,
        direction: str | None = None,
        *,
        switch_device: bool = True,
        request_id: str | None = None,
    ) -> dict[str, Any]:
        return self._submit_operation(
            "seek",
            lambda: self.seek(seconds=seconds, direction=direction, switch_device=switch_device),
            request_id=request_id,
        )

    def submit_stop(self, *, notify_device: bool = False) -> dict[str, Any]:
        return self._submit_operation("stop", lambda: self.stop(notify_device=notify_device))

    def status(self) -> dict[str, Any]:
        with self._lock:
            if self._streamer:
                with self._streamer.lock:
                    self._state.counters = dict(self._streamer.counters)
            return self._state.to_json()

    def stop(self, *, notify_device: bool = False) -> dict[str, Any]:
        with self._lock:
            if self._process:
                self._process.stop()
                self._process = None
            if self._streamer:
                self._streamer.stop()
                self._streamer = None
            self._state.active = False
            self._state.mode = "idle"
            result = {"ok": True, "status": self._state.to_json()}
        if notify_device:
            result["device"] = self.device.notify_playback_stopped().__dict__
        return result

    def play_audio(self, item: MediaItem, start_s: float = 0, *, switch_device: bool = True) -> dict[str, Any]:
        if item.kind != "audio":
            return {"ok": False, "error": "item is not audio"}
        with self._lock:
            self.stop()
            time.sleep(0.05)
            device = self.device.switch("audio") if switch_device else None
            command = audio_tcp_command(item.path, self.config, start_s)
            self._process = ProcessHandle(command, dry_run=self.dry_run)
            self._state = self._make_state("audio_player", item, start_s, command=command)
            return {
                "ok": True if device is None else device.ok,
                "device": {"ok": True, "app": "audio", "body": "not-switched"} if device is None else device.__dict__,
                "playback": self._state.to_json(),
            }

    def play_video(
        self,
        item: MediaItem,
        *,
        audio: str | bool | None = None,
        aspect: str | None = None,
        start_s: float = 0,
        switch_device: bool = True,
        profile: str | None = None,
        fps: float | None = None,
        jpeg_quality: int | None = None,
    ) -> dict[str, Any]:
        if item.kind != "video":
            return {"ok": False, "error": "item is not video"}
        with self._lock:
            self.stop()
            time.sleep(0.05)
            wants_audio = self._resolve_audio_request(audio)
            use_sync = bool(wants_audio and item.has_audio)
            selected_aspect = aspect or self.config.defaults.video_aspect
            selected_profile, selected_fps, selected_quality = resolve_video_tuning(
                self.config,
                synced=use_sync,
                profile=profile,
                fps=fps,
                jpeg_quality=jpeg_quality,
            )
            if use_sync:
                device = self.device.switch("sync") if switch_device else None
                streamer = SyncedAVStreamer(
                    item,
                    self.config,
                    start_s=start_s,
                    aspect=selected_aspect,
                    profile=selected_profile,
                    fps=selected_fps,
                    jpeg_quality=selected_quality,
                    dry_run=self.dry_run,
                )
                commands = [part for cmd in streamer.commands for part in ["&&", *cmd]][1:]
                if not streamer.start():
                    with streamer.lock:
                        startup = dict(streamer.counters)
                    streamer.stop()
                    errors = [startup.get("audio_error"), startup.get("video_error")]
                    detail = "; ".join(str(error) for error in errors if error) or "AV listeners did not become ready"
                    self._state = self._make_state(
                        "video_error", item, start_s, aspect=selected_aspect, with_audio=True, command=commands
                    )
                    self._state.active = False
                    self._state.counters = startup
                    return {
                        "ok": False,
                        "error": f"synchronized playback startup failed: {detail}",
                        "device": {"ok": True, "app": "video", "body": "not-switched"}
                        if device is None else device.__dict__,
                        "playback": self._state.to_json(),
                    }
                self._streamer = streamer
                self._state = self._make_state("video_player", item, start_s, aspect=selected_aspect, with_audio=True, command=commands)
            else:
                device = self.device.switch("stream") if switch_device else None
                command = graphics_video_tcp_command(
                    item.path,
                    self.config,
                    start_s=start_s,
                    aspect=selected_aspect,
                    fps=selected_fps,
                    quality=selected_quality,
                )
                self._process = ProcessHandle(command, dry_run=self.dry_run)
                self._state = self._make_state("graphics_player", item, start_s, aspect=selected_aspect, with_audio=False, command=command)
            self._state.counters.update(
                {"profile": selected_profile, "fps": selected_fps, "jpeg_quality": selected_quality}
            )
            device_json = {"ok": True, "app": "video", "body": "not-switched"} if device is None else device.__dict__
            return {"ok": True if device is None else device.ok, "device": device_json, "playback": self._state.to_json()}

    def play_image(
        self,
        item: MediaItem,
        *,
        aspect: str | None = None,
        seconds: float | None = None,
        mode: str = "oneshot",
        switch_device: bool = True,
        profile: str | None = None,
        fps: float | None = None,
        jpeg_quality: int | None = None,
    ) -> dict[str, Any]:
        if item.kind != "image":
            return {"ok": False, "error": "item is not image"}
        with self._lock:
            self.stop()
            device = self.device.switch("stream") if switch_device else None
            selected_aspect = aspect or self.config.defaults.image_aspect
            hold_seconds = seconds or self.config.defaults.image_hold_seconds
            selected_mode = mode.lower() if mode.lower() in {"static", "oneshot", "loop"} else "oneshot"
            animated = bool(item.metadata.get("animated"))
            selected_profile, selected_fps, selected_quality = resolve_video_tuning(
                self.config, synced=False, profile=profile, fps=fps, jpeg_quality=jpeg_quality
            )
            stream_loop = animated and selected_mode == "loop"
            image_loop = not animated
            motion_seconds = seconds or item.duration_s or self.config.defaults.motion_image_seconds
            if selected_mode == "loop":
                duration = seconds
            elif animated and selected_mode == "oneshot":
                duration = motion_seconds
            else:
                duration = hold_seconds
            command = graphics_video_tcp_command(
                item.path,
                self.config,
                aspect=selected_aspect,
                duration_s=duration,
                image_loop=image_loop,
                stream_loop=stream_loop,
                fps=selected_fps,
                quality=selected_quality,
            )
            self._process = ProcessHandle(command, dry_run=self.dry_run)
            self._state = self._make_state("image_viewer", item, 0, aspect=selected_aspect, command=command)
            self._state.counters["hold_seconds"] = hold_seconds
            self._state.counters.update(
                {
                    "animated": animated,
                    "motion_mode": selected_mode,
                    "motion_seconds": motion_seconds if animated else None,
                    "profile": selected_profile,
                    "fps": selected_fps,
                    "jpeg_quality": selected_quality,
                }
            )
            device_json = {"ok": True, "app": "stream", "body": "not-switched"} if device is None else device.__dict__
            return {"ok": True if device is None else device.ok, "device": device_json, "playback": self._state.to_json()}

    def seek(
        self,
        seconds: float | None = None,
        direction: str | None = None,
        *,
        switch_device: bool = True,
    ) -> dict[str, Any]:
        with self._lock:
            if not self._state.active or not self._state.item_id:
                return {"ok": False, "error": "nothing is playing"}
            item = self.index.get(self._state.item_id)
            if not item:
                return {"ok": False, "error": "current item disappeared from index"}
            delta = seconds
            if delta is None:
                amount = self.config.defaults.audio_seek_seconds if item.kind == "audio" else self.config.defaults.video_seek_seconds
                delta = amount if direction != "back" else -amount
            new_start = max(0, self._state.position_s() + float(delta))
            if item.kind == "audio":
                return self.play_audio(item, new_start, switch_device=switch_device)
            if item.kind == "video":
                return self.play_video(
                    item,
                    audio=self._state.with_audio,
                    aspect=self._state.aspect,
                    start_s=new_start,
                    switch_device=switch_device,
                    profile=str(self._state.counters.get("profile", "balanced")),
                    fps=float(self._state.counters.get("fps", self.config.ffmpeg.video_fps)),
                    jpeg_quality=int(self._state.counters.get("jpeg_quality", self.config.ffmpeg.video_quality)),
                )
            return {"ok": False, "error": "seek is only supported for audio and video"}

    def skip(self, direction: str, *, switch_device: bool = True) -> dict[str, Any]:
        with self._lock:
            if not self._state.item_id:
                return {"ok": False, "error": "nothing is playing"}
            item = self.index.get(self._state.item_id)
            if not item:
                return {"ok": False, "error": "current item disappeared from index"}
            target = self.index.neighbor(item, 1 if direction != "previous" else -1)
            if not target:
                return {"ok": False, "error": "no neighbor item"}
            if target.kind == "audio":
                return self.play_audio(target, switch_device=switch_device)
            if target.kind == "video":
                return self.play_video(
                    target,
                    audio=self._state.with_audio,
                    aspect=self._state.aspect,
                    switch_device=switch_device,
                    profile=str(self._state.counters.get("profile", "balanced")),
                    fps=float(self._state.counters.get("fps", self.config.ffmpeg.video_fps)),
                    jpeg_quality=int(self._state.counters.get("jpeg_quality", self.config.ffmpeg.video_quality)),
                )
            return {"ok": False, "error": "skip is only supported for audio and video"}

    def _resolve_audio_request(self, audio: str | bool | None) -> bool:
        if isinstance(audio, bool):
            return audio
        if audio is None or audio == "auto":
            return self.config.defaults.video_with_audio
        return str(audio).lower() in {"1", "true", "yes", "on", "audio"}

    @staticmethod
    def _make_state(
        mode: str,
        item: MediaItem,
        start_s: float,
        *,
        aspect: str | None = None,
        with_audio: bool | None = None,
        command: list[str] | None = None,
    ) -> PlaybackState:
        return PlaybackState(
            active=True,
            mode=mode,
            item_id=item.id,
            kind=item.kind,
            title=item.title,
            path=str(item.path),
            started_at=time.monotonic(),
            start_s=start_s,
            aspect=aspect,
            with_audio=with_audio,
            command=command,
        )
