"""Real FFmpeg + loopback NAV1 transition smoke test; does not contact the device."""
from dataclasses import replace
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from server.poko_server.config import load_config
from server.poko_server.ffmpeg_tools import default_ffmpeg_executable
from server.poko_server.models import MediaItem
from server.poko_server.playback import SyncedAVStreamer


def main():
    config = load_config(ROOT / "server/config.example.yml")
    ffmpeg = default_ffmpeg_executable()
    output = ROOT / "build/host-tests/streams"
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for audio in (True, False):
        path = output / ("audio.mkv" if audio else "silent.mkv")
        command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                   "-i", "testsrc2=size=128x128:rate=10"]
        if audio:
            command += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=22050"]
        command += ["-t", "2", "-c:v", "mpeg4"]
        if audio:
            command += ["-c:a", "pcm_s16le"]
        subprocess.run(command + [str(path)], check=True, timeout=20, capture_output=True)
        files.append(path)

    ports = [0, 0]
    for path, audio in zip(files, (True, False)):
        listeners = []
        threads = []
        packets = {1: [], 2: []}
        errors = []
        done = threading.Event()
        for channel in range(2):
            listener = socket.socket()
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", ports[channel]))
            ports[channel] = listener.getsockname()[1]
            listener.listen(1)
            listener.settimeout(0.2)
            listeners.append(listener)

        def receive(listener):
            try:
                while not done.is_set():
                    try:
                        connection, _ = listener.accept()
                        break
                    except socket.timeout:
                        continue
                else:
                    return
                with connection:
                    connection.settimeout(2)
                    def exact(size):
                        data = bytearray()
                        while len(data) < size:
                            chunk = connection.recv(size - len(data))
                            if not chunk:
                                return None
                            data.extend(chunk)
                        return data
                    while not done.is_set():
                        header = exact(16)
                        if header is None:
                            break
                        magic, kind, timestamp, length = struct.unpack("<4sB3xII", header)
                        assert magic == b"NAV1" and kind in (1, 2) and 0 < length <= 65535
                        payload = exact(length)
                        if payload is None:
                            break
                        if kind == 2:
                            assert payload[:2] == b"\xff\xd8" and payload[-2:] == b"\xff\xd9"
                        packets[kind].append(timestamp)
            except Exception as error:
                if not done.is_set():
                    errors.append(error)

        config = replace(config, poko=replace(config.nexus, ip="127.0.0.1",
                         video_audio_port=listeners[0].getsockname()[1],
                         video_frames_port=listeners[1].getsockname()[1]),
                         ffmpeg=replace(config.ffmpeg, executable=ffmpeg))
        item = MediaItem(id=path.stem, kind="video", path=path, title=path.stem,
                         extension=".mkv", size_bytes=path.stat().st_size, has_audio=audio)
        streamer = SyncedAVStreamer(item, config, fps=10)
        try:
            for listener in listeners:
                thread = threading.Thread(target=receive, args=(listener,), daemon=True)
                thread.start()
                threads.append(thread)
            assert streamer.start(), streamer.counters
            deadline = time.monotonic() + 6
            while len(packets[2]) < 4 and time.monotonic() < deadline and not errors:
                time.sleep(0.02)
            assert not errors, errors
            assert len(packets[2]) >= 4, streamer.counters
            assert packets[2][0] == 0 and packets[2] == sorted(packets[2]), packets[2]
            assert bool(packets[1]) == audio, packets[1]
        finally:
            streamer.stop()
            done.set()
            for thread in threads:
                thread.join(timeout=3)
            for listener in listeners:
                listener.close()
        assert all(not thread.is_alive() for thread in streamer.threads)
        assert all(process.poll() is not None for process in streamer.processes)
        print(f"PASS: {path.stem}, {len(packets[2])} JPEG frames, {len(packets[1])} audio packets; workers stopped")


if __name__ == "__main__":
    main()
