# PoKo Backend

Python backend for the PoKo ESP32-S3 device. It indexes local media folders,
serves paged JSON libraries for the firmware, switches the ESP32 app, and streams
audio/video/image content in the formats the current firmware already understands.

## Quick Start

```powershell
cd <project-root>
python -m server.poko_server --config server\config.yml
```

Default server URL:

```text
http://127.0.0.1:8765
```

Create the local configuration from the tracked template:

```powershell
Copy-Item server\config.example.yml server\config.yml
```

Set `poko.ip` and `poko.base_url` to the device, list every media directory under
`library.read_folders`, and choose a writable `library.write_folder`. Keep
`display.width` and `display.height` at `128`. For maximum detail at that fixed
resolution, use `video_quality: 4` and `graphics_quality: 4`; FFmpeg's MJPEG
quality scale is inverse, and the server clamps it to 4–15. `server/config.yml`
is ignored by Git so machine-specific paths and addresses are not committed.

The backend starts instantly and indexes in the background into SQLite. Open the
server UI here:

```text
http://127.0.0.1:8765/
```

The UI shows index progress, library counts, playback state, recent API
requests, and a config editor.

## Main ESP32 Endpoints

```text
GET  /health
GET  /
GET  /api/server/status
GET  /api/server/requests
GET  /api/index/status
GET  /api/library/audio?page=1
GET  /api/library/videos?page=1
GET  /api/library/images?page=1
GET  /api/library/texts?page=1
GET  /api/audio/{id}/play?start=0
GET  /api/video/{id}/play?audio=auto&aspect=square&start=0
GET  /api/video/{id}/play?profile=quality&fps=18&jpeg_quality=6
GET  /api/image/{id}/show?aspect=square&mode=oneshot&profile=quality
GET  /api/text/{id}
GET  /api/playback/status
GET  /api/playback/seek?direction=forward
GET  /api/playback/seek?seconds=-10
GET  /api/playback/stop
POST /api/upload
GET  /api/device/status
GET  /api/device/littlefs
GET  /api/device/littlefs/file?path=/file.txt
POST /api/device/littlefs/file
GET  /api/device/wallpaper
POST /api/device/wallpaper
DELETE /api/device/wallpaper
```

Video requests accept `profile=balanced|quality|smooth`. Expert callers may also
override `fps` and FFmpeg's `jpeg_quality` (4-15; lower means more detail). Values
are clamped to the device-safe range. The server UI controls optional high-pass
and low-pass filters; filtering happens in FFmpeg and therefore costs no ESP32 CPU.

`/api/upload` intentionally returns `501` for now. The write folder exists in
the config so camera/voice-recorder upload can be added cleanly later.

## Playback Mapping

- Audio files switch the ESP32 to `audio` and stream MP3 over TCP port `1235`.
- Videos with audio switch to `sync` and use the timestamped true video player:
  PCM mono on `1236`, MJPEG frames on `1237`.
- Videos without audio, or requests with `audio=false`, switch to `stream` and
  use the Graphics Player on port `1234`.
- Static images are decoded directly by the Gallery. Animated GIF/WebP/APNG/MJPEG
  can use `mode=static|oneshot|loop`; one-shot is the default. The device preloads
  a static poster, releases Graphics Player after one sequence, and resumes the
  normal gallery with that poster.

The LittleFS page includes a 128 x 128 wallpaper cropper. Wallpaper is stored as
`/wallpaper.jpg`, can be enabled or disabled from either system settings or the
server UI, and is drawn only below the persistent PoKo title bar.

## Test Scripts

Run unit tests:

```powershell
python server\run_tests.py
```

Run a random playback smoke test against the local backend. Start the server with
`--dry-run` first if you want this to avoid touching the ESP32:

```powershell
python server\scripts\random_playback.py --server http://127.0.0.1:8765 --all
```

With a real server run, the script will switch apps and stream to the board.
