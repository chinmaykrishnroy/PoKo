<div align="center">
  <img src="poko_icon.svg" width="96" height="96" alt="PoKo icon"/>
  <h1>PoKo</h1>
  <p><strong>Possibly the smallest video-playing device ever built on a microcontroller.</strong></p>
  <p>
    Music · Video · Gallery · Snapclient · Analyzer · Tuner · Web UI<br>
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
| Audio output codec | ES8311 (I²S DAC, hardware volume control) |
| Microphones | Dual microphone array through ES7210 ADC (I²C 0x40, I²S DIN GPIO11) |
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
| **Pixels** | NeoPixel ring studio and audio-reactive effects |
| ⚙️ **Settings** | Brightness, volume, theme, Wi-Fi, button controls, and more |
| **Analyzer** | Microphone oscilloscope, logarithmic FFT, octave/third-octave bands, dBFS level meter |
| **Tuner** | A440 note/cents tuner and major, minor, sus2, sus4, 7, maj7, m7, augmented chord estimates |

The launcher keeps **eight visible navigation dots** for ten apps. The represented
window scrolls at its edges: the selected dot stays last when advancing past the
window and first when moving back past it.

### Microphone Analyzer and Tuner

These tools automatically observe **Music or active SSync playback PCM** without
stopping it, changing its volume, or taking its I²S ownership. The header shows
`MUSIC PCM` / `SSYNC PCM`. With no playing stream, they use the board's **ES7210
microphone** and work without a media server. Music retains PCM observation
through pauses; it shows `--` while no samples arrive. Idle SSync can yield to
the microphone; new audible SSync input automatically resumes output and switches
the tool back to PCM. Microphone gain controls apply only to microphone input. Starting Music from
an updated server while a tool is open keeps the tool visible and switches its
input to PCM.

`2R` cycles Analyzer's Scope → Log FFT → Octave
→ Level modes, or Tuner's Note → Chord modes; `2L` returns home. Single `L`/`R`
adjusts microphone gain in 3 dB steps (0–30 dB; default 18 dB). Tuner also uses
automatic gain: it raises a quiet recognized note/chord above the learned ambient
floor gradually; tonal interference alone is insufficient, and it does not raise gain on
silence/broadband noise, and reduces gain quickly on clipping. `AUTO` shows the current gain;
manual changes override it for five seconds (`MAN`). Automatic gain changes are
not saved to preferences; your explicit manual gain is saved.

| Analyzer mode | Hold L / R |
|---|---|
| Oscilloscope | Shorter / longer window: 2, 5, 10, 20, 50, 100, 160 ms (new default 5 ms) |
| Log FFT | 1024 → 2048 → 4096 → 8192 → Bass 8192 |
| Octave | Full octave / third octave |
| Level | Fast / slow RMS display response |

Scope uses a connected, triggered waveform trace, with box-filtered resampling
when multiple input samples occupy a pixel. Its vertical scale fits the displayed
waveform with about 5% headroom and shrinks after a quiet settling period.
Existing saved time windows are retained; 5 ms shows about two cycles of A440,
whereas 20 ms shows about nine cycles on the same 118-pixel plotting width.
FFT/octave plots initially place their tallest bar at **80% height**, reserve the
upper 20% for growth, and expand/recenter only if a peak exceeds that headroom.
Axes remain labeled in dBFS. The scope also shows the smoothed measured period (`Avg T`, ms)
when a periodic signal is detected; FFT shows its strongest peak frequency in
small type (`Pk`, Hz), including fractional Hz below 1 kHz. Normal FFT spans
20 Hz–20 kHz on a logarithmic axis (8192 samples at 48 kHz give 5.86 Hz bins).
Holding R beyond 8192 selects **Bass**: 20–2,000 Hz and a 63-tap anti-alias filter.
At the microphone's 48 kHz clock, its 12 kHz analysis rate gives **1.46 Hz bins**
from about **0.68 seconds** of history. At 44.1 kHz playback the bins are **1.35 Hz**
from about **0.74 seconds**; labels follow the actual clock.
Decimal peaks are sub-bin estimates, not a promise of 0.01 Hz resolution.
Octave centers span 31.25 Hz–16 kHz. The level meter shows RMS and peak **dBFS** plus
clipping, not calibrated acoustic dB SPL. Hold L/R in Tuner adjusts gain.

Tuner uses A4=440 Hz and displays note, octave, frequency and cents (approximately
40–2000 Hz). Chord mode is an estimate for a clearly played chord, not polyphonic
transcription of a full song. Noise, weak/missing notes, distortion and ambiguous
voicings can prevent or change a match; symmetric augmented chords are named
using the detected bass. Two agreeing detections confirm a chord; a change of quality on the same root
requires at least three confirmations and additional confidence for weak matches
to reject transient upper partials. A brief hold
bridges missed frames, then clears to `--`. A rich single note is rejected as a
chord. Silence or unavailable input shows `--` in the same fixed layout.
Pitch uses [YIN](https://www.ee.columbia.edu/~dpwe/papers/deChevK02-yin.pdf) with
spectral refinement; refinement uses spectral strength rather than an unrelated
time-domain transient. Note readings are smoothed without snapping them to A440.
Tuning color blends from green (within 2 cents) through amber toward red as error
grows. Spectrum colors run from bass cyan through blue to treble violet; level
meter warning colors follow actual dBFS/clipping, independently of autoscaling.

Microphone input is captured at 48 kHz/16-bit stereo; analysis uses MIC1 rather than averaging
the two microphones and introducing phase cancellation. The microphone takes
exclusive ownership of I²S0: SSync is suspended and resumed on exit; Music/Video
use their existing stop/handoff path. The speaker PA remains off during capture.
Active capture blocks idle system sleep/auto-off above the configured battery
threshold; display timeouts and low-battery protection still apply. Settings save
on exit when idle; an active PCM observer defers flash writes until playback is
idle or an orderly shutdown. DSP/canvas buffers are released on exit. The tools do not auto-open
after reboot.

Authenticated `GET /api/acoustic` reports capture state, frame count, input age,
level, peak frequency, period, FFT bin/range, raw versus displayed tuner readings,
automatic-gain status and speaker-PA state for on-device verification.
`input_source`, the actual `sample_rate`, and `input_overruns` distinguish
microphone RX from the passive playback observer. Playback is analyzed at its
actual clock (including 44.1 and 48 kHz), not relabeled as 48 kHz; a stable
channel is selected to avoid cancellation of anti-phase stereo. Overflow of the
observer only discards analysis copies, never playback. It also reports RX
overruns, capture timestamps and DSP duration. Capture gaps
invalidate the analysis history; pitch is never estimated across known lost
samples. Tuner skips the Analyzer-only bass decimator, and RX buffers cover about
171 ms of short service-loop stalls.
It exposes no raw microphone recording and requires the device admin login.

For opt-in hardware checks, configure the private `.env` from `.env.example`.
`python server/deploy/verify_acoustic.py --env .env` explicitly stops device
playback to check microphone RX continuity and repeated ownership handoffs.
With continuous playback already running, use
`python server/deploy/verify_passive_audio.py --source music --env .env` (or
`--source ssync`) to verify both tools while preserving that stream. Neither
script records microphone audio. Chord mode remains an estimate; a noisy or
ambiguous guitar decay may deliberately show `--`.
The [v2.5.0 hardware validation](docs/acoustic-validation-v2.5.0.md) records
passed playback checks and a remaining microphone chord-identification failure;
chord accuracy is not yet verified for general instrument use.


Hardware references: [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-LCD-0.85),
[board schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-0.85/ESP32-S3-LCD-0.85-Schematic.pdf),
and [Espressif ES7210 register/clock reference](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es7210/es7210.c).

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

Firmware modules use declaration-only `.h` interfaces and matching `.cpp`
implementations. Mutable hardware state must have exactly one owner in a `.cpp`
file; headers may contain types, declarations, constants, and templates only.
`tests/check_header_boundaries.py` enforces this boundary across every
first-party firmware module. Template-only and third-party C headers are
explicit exceptions.

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
│               ├─ AcousticApp (Analyzer/Tuner) │
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

Download the application binary from a GitHub release for OTA; use a complete Arduino build for initial USB flashing:

```bash
# OTA (device must already be running PoKo and on Wi-Fi)
curl --digest --user admin -X POST http://<DEVICE_IP>/ota/upload \
  -F "update=@<DOWNLOADED_RELEASE_APPLICATION.bin>"

# First flash via USB
arduino-cli upload -p <SERIAL_PORT> --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" --input-dir dist .
```

For a first USB flash, use the complete Arduino build output (`dist`) so the
uploader places the bootloader, partition table, and application correctly.
The application-only `.ino.bin` is for OTA; do not flash it at offset `0x0`.

On first boot the device creates a Wi-Fi AP named **PoKo-Setup** — connect to it and visit `http://192.168.4.1/` to set your Wi-Fi credentials.

New devices use **`admin` / `00000000`** for their initial Web UI login.
Sign in to configure Wi-Fi, server settings, or OTA, and change that initial password.
The media server uses a separate device token over HTTPS. See
[secure local-server setup](server/SECURE_LOCAL.md) and
[persistent Pi/cloud deployment](server/deploy/README.md). The browser's server
login password, device token, and device-admin password serve different roles.

Use **Web UI → Settings → Security** to change the device-admin password.
Enter the current password and confirm a new password of **exactly 8 printable,
non-space ASCII characters**. Physical **Settings → Security → Reset password**
generates a random eight-character password; Show password displays the saved one.
Existing longer passwords remain valid until changed; upgrading never resets them.

New secure servers also start with **`admin` / `00000000`**, but this login only
opens setup. Read the private `setup-code` on the server machine and choose a
16–128 character server passphrase before media, settings or controls unlock.
On the Pi example deployment: `sudo cat /var/lib/poko/setup-code`.

To connect PoKo, open **Server Web UI → Settings → Device tokens → Create device
token**, copy the token shown once, and paste it into **Device Web UI → Media &
Network → Device token**, then click **Update Server**. Each device can have its
own token. Revoke lost tokens from server Settings; passwords and other devices
are unaffected. A token authorizes media requests, not server administration.
Server-machine password recovery is always available through
`python server/scripts/rotate_admin.py /path/to/private-credentials --prompt`;
restart the server afterward. Use `--bootstrap` instead to restore initial setup
with `00000000` and a new private claim code.
Both dashboards use a normal login form with **Remember me** and **Sign out**.
Sessions use HttpOnly cookies, never localStorage. A tab switch does not end a
session. Remembered sessions last up to 30 days; server restarts require a fresh
server login. Device remembered sessions can survive reboots after clock sync.
The device already saves its password internally: open **Settings → Security**,
then choose **Show password**. Long passwords have L/R pages. Password reset is
a separate option requiring confirmation. Leaving the screen hides the password.
The server dashboard's Settings also supports changing its browser passphrase;
its private verifier path must be configured (the deployment helpers do this).
Existing server media controls remain paired
using a restricted integration credential that cannot access administration or
OTA. Reboot after a password change to update ArduinoOTA authentication too.
Perform password changes on a trusted LAN: the device dashboard uses HTTP.

For optional live verification, copy [.env.example](.env.example) to `.env`, edit
your own targets and credential-file paths, and follow the
[Pi reference architecture](server/deploy/README.md). Test scripts accept
`--env /path/to/private.env`; your private deployment values stay outside Git.

Server Web UI playback uses an authenticated metadata handshake with firmware
v2.2 or later. It first synchronizes the selected item and prepares the device's
listener, then starts the stream; the browser shows Starting until that operation
succeeds. New selections replace pending commands. This is a playback controller,
not a FIFO song queue. Generated firmware binaries live in CI/release assets;
the local `build/` directory is ignored by Git.

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
  video_fps: 30
  video_quality: 4         # 4 = highest supported JPEG detail
  graphics_fps: 20
  graphics_quality: 4
```

The server runs on port **8765** by default. Set it in the PoKo web UI under **Controls → Media Server Address**.

### Continuous integration and releases

GitHub Actions runs backend tests, 41 firmware host regressions, the FFmpeg
loopback smoke test, and a full ESP32-S3 compile for pull requests and pushes to
`main`. Each successful run stores the application `.bin` as a workflow artifact.

Pushing an annotated semantic-version tag such as `v1.3.2` runs the same gates,
checks that `InfoApp.cpp` and `PokoAPI.cpp` contain that version, and publishes a
GitHub Release with `PoKo-v1.3.2.bin` and its SHA-256 checksum. Upload the
application `.bin` through `/ota`; the merged, bootloader, and partition images
are for USB provisioning and are not OTA images.

---

## Documentation

- [**Complete Device & Microcontroller Manual (DEVICE_MANUAL.md)**](DEVICE_MANUAL.md) — Comprehensive reference covering ESP32-S3 architecture, FreeRTOS tasks, layered idle power management, on-screen dots (Wi-Fi, audio, carousel), detailed walkthrough of all 10 apps, power telemetry, all 23 Settings entries, reboot/shutdown procedures, and REST API.
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
├── AcousticApp.*          # Microphone Analyzer and A440 Tuner screens
├── AcousticDSP.*          # FFT, YIN pitch, chroma/chord estimates, auto range
├── AcousticPresentation.* # Reading stability, auto gain, scale and color policy
├── MicrophoneInput.*      # Exclusive ES7210 ADC / I²S RX driver
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
└── .github/workflows/ci-release.yml # Checks, firmware artifacts and tagged releases
```

---

## License

[MIT](LICENSE) © 2026 Chinmay Krishn Roy
