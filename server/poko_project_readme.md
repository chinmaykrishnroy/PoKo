# PoKo Project README

PoKo is a pocket media device built around the **Waveshare ESP32-S3-LCD-0.85**. Its 128×128 GC9107 display, three physical buttons, ES8311 audio codec, speaker amplifier, battery monitoring, and eight-pixel RGB ring provide a compact interface for music, synchronized video, photos, Snapcast, time, settings, and system information.

This Markdown file can also be used as representative text content when testing the PoKo server's media library and documentation handling.

## Hardware

The current firmware targets an ESP32-S3 with 8 MB flash and octal PSRAM. The device uses:

- A 128×128 GC9107 IPS display.
- Left, right, and power buttons.
- An ES8311 codec with I2S audio output.
- An eight-pixel RGB ring.
- Battery voltage and charging-state monitoring.
- Wi-Fi connectivity to the optional PoKo server.

## Device Apps

PoKo provides eight apps from its launcher:

1. **Info** — network, memory, battery, audio, and firmware status.
2. **Clock** — a synchronized digital clock.
3. **SSync** — direct Snapcast playback.
4. **Music** — indexed music browsing and playback from the PoKo server.
5. **Video** — synchronized 128×128 video and audio playback.
6. **Gallery** — local LittleFS images and server-indexed photos.
7. **Pixels** — RGB color, animation, and music-reactive controls.
8. **Settings** — display, power, audio, network, gallery, and device actions.

The launcher and apps use partial redraws and canvases to keep the small display responsive and reduce flicker.

## Physical Controls

The left and right buttons navigate lists and change app-specific values. Click, double-click, hold, and two-button chord actions are interpreted by the firmware's central input module. The power button wakes the display, performs configured power actions, and participates in recovery behavior.

When the display is asleep, any physical button can wake it. The wake gesture is consumed so the same press does not accidentally activate an app action.

## PoKo Server

The optional Python server in the `server` directory performs work that is expensive for the ESP32:

- Indexes configured music, video, and image folders in SQLite.
- Reads metadata with FFprobe.
- Generates thumbnails.
- Transcodes audio and 128×128 video with FFmpeg.
- Streams audio, video frames, and synchronized timestamps to the device.
- Provides a browser UI for configuration, playback, device controls, and status.

The firmware continues to provide local apps and settings when the server is unavailable. Music, Video, and Gallery distinguish an unreachable server from missing media and refresh stale catalog entries when files move.

## Playback Transports

PoKo uses dedicated TCP transports:

- Port `1234` for graphics and silent motion-image frames.
- Port `1235` for Music MP3 data.
- Port `1236` for synchronized Video PCM audio.
- Port `1237` for synchronized Video JPEG frames.
- Port `1704` for Snapcast by default.

Synchronized video treats audio as the master clock. Late video frames may be dropped to preserve audible continuity and prevent accumulated latency.

## Configuration

Copy `server/config.example.yml` to `server/config.yml`, then set the device address and media folders. The device section uses the `poko` key:

```yaml
server:
  host: 0.0.0.0
  port: 8765

poko:
  ip: 192.168.0.16
  base_url: http://192.168.0.16
  switch_delay_ms: 500
  ports:
    graphics: 1234
    audio: 1235
    video_audio: 1236
    video_frames: 1237

display:
  width: 128
  height: 128

library:
  read_folders:
    - ./media/music
    - ./media/videos
    - ./media/pictures
  write_folder: ./media/uploads
  page_size: 10
```

See `server/README.md` and `server/config.example.yml` for the full configuration, quality profiles, and startup commands.

## Reliability and Power

Long-running media activity holds explicit power locks so automatic display sleep or shutdown does not interrupt playback. Low-battery automatic shutdown is configurable and only applies after the battery remains below its configured threshold while the device is idle and not charging.

Audio ownership is centralized so Music, Video, and SSync release I2S cleanly before another source starts. Server failures stop stale playback, clear misleading artwork where appropriate, and present retry or back actions on the device.

## Web UI and OTA

The firmware serves a local browser dashboard and OTA upload page. OTA updates hold power and display locks during the transfer. Use a firmware `.bin` produced by the repository's release workflow and verify its published SHA-256 checksum before uploading it.

## Development Checks

Run backend tests:

```powershell
python server\run_tests.py
```

Run host-side firmware regressions:

```powershell
python tests\firmware\run_tests.py
```

Run the stream smoke test:

```powershell
python tests\firmware\stream_smoke.py
```

The release workflow runs these checks, enforces declaration-only firmware headers, compiles the ESP32-S3 firmware, and attaches the binary and checksum to versioned GitHub releases.
