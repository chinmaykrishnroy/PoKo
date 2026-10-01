<div align="center">
  <img src="poko_icon.svg" width="96" height="96" alt="PoKo icon"/>
  <h1>PoKo</h1>
  <p><strong>Possibly the smallest video-playing device ever built on a microcontroller.</strong></p>
  <p>
    Music · Video · Gallery · Snapclient · Clock · OTA · Web UI<br>
    all running on a <b>128 × 128 px</b> display the size of a coin.
  </p>

  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg">
  <img alt="Platform" src="https://img.shields.io/badge/platform-ESP32--S3-orange.svg">
  <img alt="Display" src="https://img.shields.io/badge/display-128×128-green.svg">
  <img alt="Language" src="https://img.shields.io/badge/firmware-Arduino-teal.svg">
  <img alt="Server" src="https://img.shields.io/badge/server-Python%203-yellow.svg">
</div>

---

## What is PoKo?

PoKo is a **full-featured pocket media device** running on an ESP32-S3 microcontroller with a 0.85-inch IPS display. It streams video and audio from a local Python media server over Wi-Fi, plays synchronized multi-room audio via Snapclient, shows a photo gallery stored on the device, and exposes a polished web control panel — all on hardware you can buy for under ₹1,800 / ~\$20.

> **Why is this impressive?**  
> Real-time MJPEG video decoding, PCM audio output through an ES8311 codec DAC, server-indexed media library, LittleFS photo storage, OTA firmware updates, and a theme-aware web UI — simultaneously, on a microcontroller with no OS.

---

## Hardware

| Component | Detail |
|-----------|--------|
| SoC | ESP32-S3 (dual-core 240 MHz, OPI PSRAM 8 MB, Flash 8 MB) |
| Display | 0.85" GC9107 IPS LCD — **128 × 128 px** |
| Audio codec | ES8311 (I²S DAC/ADC, hardware volume control) |
| Microphones | Dual MEMS microphone array |
| LED ring | Surround RGB LEDs |
| Connectivity | Wi-Fi 802.11 b/g/n |

### Buy the board

[**Waveshare ESP32-S3-LCD-0.85 — Robu.in**](https://robu.in/product/waveshare-esp32-s3-lcd-085-en-development-board-085-128x128-ips-lcd-dual-microphone-array-surround-rgb-leds-for-ai-voice-and-edge-computing/)  
₹1,789 in India · ≈ \$20 USD

---

## Features

### On-device Apps

| App | Description |
|-----|-------------|
| 🏠 **Launcher** | Carousel UI with GFX-drawn icons for every app |
| ℹ️ **Info** | Live system stats — IP, heap, PSRAM, uptime, CPU |
| 🕐 **Clock** | IST digital/analog clock with NTP sync |
| 🔊 **SSync** | Snapclient — synchronized multi-room audio from Snapserver |
| 🎵 **Music** | Browse & stream MP3 audio from the media server with album art |
| 🎬 **Video** | Stream MJPEG video with synchronized PCM audio |
| 🖼️ **Gallery** | View LittleFS-stored photos + server-indexed images |
| ⚙️ **Settings** | Brightness, volume, theme, Wi-Fi, button controls, and more |

### Web Control Panel (`http://<device-ip>/`)

- **Dashboard** — live health metrics, quick app launcher
- **Applications** — launch any app remotely
- **Controls** — brightness & volume sliders, RGB LED, Snapcast config, media server address, Wi-Fi
- **Gallery manager** — upload, preview and delete 128 × 128 JPEG photos stored in LittleFS
- **Settings** — dark/light theme toggle, OTA update, driver reset, reboot
- Responsive design · Sidebar navigation · Favicon · Dark + light theme

### Firmware

- OTA firmware updates over Wi-Fi (`/ota`)
- LittleFS filesystem for photos (1.5 MB partition)
- Three-button navigation with single-, double-, hold-, and chord gestures
- Wake from L, R, or PWR with the wake gesture consumed before normal input
- Music-style scrolling titles in Music, Video, and Gallery
- Persistent preferences (brightness, volume, Wi-Fi credentials, theme…)
- PSRAM-backed decode buffers for smooth video

---

## Architecture

```
┌──────────────────────────────────────────────┐
│                 ESP32-S3 (PoKo)              │
│                                              │
│  PokoUI  ──► App State Machine               │
│               ├─ MusicApp   (TCP MP3 stream) │
│               ├─ VideoApp   (MJPEG + PCM)    │
│               ├─ GalleryApp (LittleFS + srv) │
│               ├─ SSyncApp   (Snapclient)     │
│               ├─ ClockApp   (NTP)            │
│               ├─ InfoApp                     │
│               └─ SettingsApp                 │
│                                              │
│  PokoAPI ──► HTTP endpoints (/api/*)         │
│  PokoWebUI ► Embedded web control panel      │
│  PokoOTA ──► OTA update handler              │
└──────────────┬───────────────────────────────┘
               │ Wi-Fi (TCP / HTTP)
               ▼
┌──────────────────────────────────────────────┐
│           Python Media Server                │
│   server/poko_server/                        │
│   ├─ http_api.py  — REST API                 │
│   ├─ playback.py  — FFmpeg stream manager    │
│   ├─ media_index.py — library scanner        │
│   ├─ ffmpeg_tools.py — transcoding           │
│   └─ config.py   — YAML configuration        │
└──────────────────────────────────────────────┘
```

---

## Getting Started

### 1 — Flash the firmware

Grab the latest binary from this repo and flash via the web OTA page or `esptool`:

```bash
# OTA (device must already be running PoKo and on Wi-Fi)
curl -X POST http://<DEVICE_IP>/ota/upload \
  -F "update=@build/esp32.esp32.esp32s3/PoKo.ino.bin"

# First flash via USB
esptool.py --chip esp32s3 write_flash 0x0 build/esp32.esp32.esp32s3/PoKo.ino.bin
```

On first boot the device creates a Wi-Fi AP named **PoKo-Setup** — connect to it and visit `http://192.168.4.1/` to set your Wi-Fi credentials.

### 2 — Build from source

**Dependencies (Arduino CLI):**

```bash
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "ArduinoJson@7.4.3" "TJpg_Decoder@1.1.0" "U8g2@2.35.30" "GFX Library for Arduino@1.6.8" "FastLED@3.10.1" "OneButton@2.6.1" "ESP8266Audio@2.4.1"
```

**Compile:**

```bash
arduino-cli compile \
  -b esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB \
  --export-binaries .
```

### 3 — Run the media server

```bash
python -m pip install -r server/requirements.txt
copy server\config.example.yml server\config.yml  # Windows
python -m server.poko_server --config server/config.yml
```

`server/config.yml` is deliberately ignored by Git because it contains local
paths and device addresses. Copy [server/config.example.yml](server/config.example.yml)
and edit at least the device address and media folders:

```yaml
poko:
  ip: 192.168.0.4          # your device IP
  base_url: http://192.168.0.4

library:
  read_folders:
    - /mnt/media/music
    - /mnt/media/videos
    - /mnt/media/pictures
  write_folder: /mnt/media/videos
  db_path: poko.db

display:
  width: 128
  height: 128

ffmpeg:
  video_fps: 20
  video_quality: 4         # 4 = highest supported JPEG detail
  graphics_fps: 20
  graphics_quality: 4
```

The server runs on port **8765** by default. Set it in the PoKo web UI under **Controls → Media Server Address**.

### Continuous integration and releases

GitHub Actions runs backend tests, 40 firmware host regressions, the FFmpeg
loopback smoke test, and a full ESP32-S3 compile for pull requests and pushes to
`main`. Each successful run stores the application `.bin` as a workflow artifact.

Pushing an annotated semantic-version tag such as `v1.3.2` runs the same gates,
checks that `InfoApp.h` and `PokoAPI.h` contain that version, and publishes a
GitHub Release with `PoKo-v1.3.2.bin` and its SHA-256 checksum. Upload the
application `.bin` through `/ota`; the merged, bootloader, and partition images
are for USB provisioning and are not OTA images.

---

## Documentation

- [**Complete Device & Microcontroller Manual (DEVICE_MANUAL.md)**](DEVICE_MANUAL.md) — Comprehensive reference covering ESP32-S3 architecture, FreeRTOS tasks, dynamic resource/power management, on-screen dots (Wi-Fi, audio, carousel), detailed walkthrough of all 8 apps, power telemetry, all 17 system preferences, reboot/shutdown procedures, and REST API.
- [**Button Controls & Navigation Guide (BUTTON_CONTROLS.md)**](BUTTON_CONTROLS.md) — Physical button layout, debounce timings, and quick gesture matrix.
- [**Release Versioning & Git Tags (VERSIONING.md)**](VERSIONING.md) — Semantic versioning policy and automated release workflows.

---

## Repository Layout

```
Poko/
├── PoKo.ino               # Main sketch — setup(), loop(), app switching
├── DEVICE_MANUAL.md       # Complete device & microcontroller manual
├── BUTTON_CONTROLS.md     # Physical button controls guide
├── PokoUI.h               # Launcher carousel with GFX-drawn icons
├── PokoAPI.h              # HTTP REST API endpoints
├── PokoWebUI.h            # Embedded web control panel (single-file HTML/JS/CSS)
├── PokoTheme.h            # Dark / light colour palettes
├── PokoAppState.h         # App state enum & transition helpers
├── PokoDrivers.h          # Display, audio codec, LED ring init
├── MusicApp.h             # Audio player
├── VideoApp.h             # Video player
├── GalleryApp.h           # Photo gallery (LittleFS + server)
├── SSyncApp.h             # Snapclient multi-room audio
├── ClockApp.h             # NTP clock
├── InfoApp.h              # System info
├── SettingsApp.h          # On-device settings
├── SnapPlayer.h           # Snapcast protocol client
├── SyncedAVPlayer.h       # Synchronized audio+video TCP player
├── TCPAudio.h             # Raw TCP audio listener
├── ButtonInput.h          # Encoder + button debounce
├── es8311.*               # ES8311 codec driver
├── poko_icon.svg          # App icon
├── server/
│   ├── config.yml         # Server configuration
│   └── poko_server/       # Python media server package
│       ├── http_api.py
│       ├── playback.py
│       ├── media_index.py
│       ├── ffmpeg_tools.py
│       ├── database.py
│       └── config.py
└── build/
    └── esp32.esp32.esp32s3/
        └── PoKo.ino.bin   # Pre-built release binary
```

---

## License

[MIT](LICENSE) © 2026 Chinmay Krishn Roy
