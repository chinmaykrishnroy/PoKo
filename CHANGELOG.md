# Changelog

All notable changes to **Poko** firmware are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v1.3.2] — 2026-09-30

### Fixed
- Physical L/R doubles now use the documented 450 ms window; GPIO initialization
  is deferred to setup and a 5 ms input worker queues actions for the main loop.
- Any button wakes a sleeping display without triggering its normal action;
  Right also participates in deep-sleep wake. Unused PWR double recognition removed.
- Info Hold-R refreshes metrics; repeat holds cannot change volume in unrelated apps.
- Audio ownership transitions no longer hold the state mutex while waiting for
  SSync workers, and failed worker shutdown cannot hand I2S to another producer.
- Video changes stop old transports before resetting buffers; startup errors
  recover to the browser. Backend stop unblocks senders, and silent clips keep
  the embedded player's NAV1 video transport.
- Gallery and Video use a bounded Music-style title marquee; Gallery allows a
  complete long-title pass before entering fullscreen.
- Windows backend bind is exclusive so a second server cannot start workers on
  an already occupied port.
- Hardened backend config parsing/round-tripping, atomic reloads, database-path changes, and invalid-request handling.
- Prevented partial media scans from purging the catalog; fixed case-sensitive media IDs and automatic migration of legacy ID collisions.
- Hardened FFmpeg lifecycle handling, failed device switches, process cleanup, safe image durations, and synchronized-stream shutdown.
- Fixed camera JPEG validation/listing edge cases and configuration serialization duplication.
- Hardened firmware audio ownership, TCP/SyncedAV worker shutdown, driver reset safety, brightness/volume state synchronization, and power-setting bounds.
- Restored host-side OneButton test support and expanded regression coverage for the repaired failure modes.

### Validation
- Passed 30 firmware host regressions and 56 backend tests; two case-sensitive
  filename cases are skipped on Windows.
- Passed real FFmpeg loopback streaming for audio/video and silent-video NAV1
  sessions, including sender shutdown between videos.
- Compiled the ESP32-S3 application image and installed it through Web OTA;
  the device rebooted and returned a healthy API response.

## [v1.2.0] — 2026-09-28

### Added
- **Themed loading screens** — Two completely distinct designs, one per theme:
  - **Dark**: Pure black background, subtle cyan outline border, glowing cyan accent pill, white text rendered directly on black (no filled card surface to cause fading artefacts).
  - **Light**: Crisp white background, off-white card (`0xF7BE`), deep navy accent pill, jet-black text.
  - App name shown in the loading label (e.g. `Loading SSync...`), falls back to `Loading App...` if too wide.
- **Real-time InfoApp refresh** — All system rows (uptime, battery %, charging state, WiFi, time) now rebuild every 250 ms; charging state change triggers an immediate rebuild.
- **Theme-aware accent colors** — New inline helpers `pokoClrGreen()`, `pokoClrWarn()`, `pokoClrCyan()`, `pokoClrErr()` return deep/dark variants on light theme and bright variants on dark theme. Applied across `SSyncApp`, `InfoApp`, `MusicApp`, `GalleryApp`, `PixelApp`, `AudioManager`.
- **Per-app tile accent colors** — Launcher carousel icons use deep-contrast per-app colors in light mode (dark forest green for SSync, deep navy for Clock, etc.).
- **`getAppName()` helper** in `Poko.ino` — maps `AppState` enum to a human-readable string used by the loading screen.
- **`BUTTON_CONTROLS.md`** — Comprehensive reference for all button gestures, combos, and per-app mappings.

### Fixed
- **Dark loading screen "faded card"** — Removed filled card surface (`0x1082`); text now drawn on pure black so no lighter overlay is visible.
- **Settings app header missing bottom border** — `fillRect` height extended from 13 → 14 px; `drawFastHLine` separator added at y=13.
- **InfoApp double-click exit** — Added unified rapid-click timestamp tracking (`INFO_EXIT_MS = 600ms`) across both `L` and `R` buttons. Two quick clicks on either button immediately exit the app, while single clicks smoothly scroll up/down.
- **Power-aware CPU scaling & Perf indicator** — Implemented dynamic CPU frequency throttling: automatically scales to 160 MHz when running on battery power to conserve energy, and returns to 240 MHz (MaxPerf) when plugged into USB power/charging. Updated InfoApp `Perf` row to reflect the real hardware operational state (`USB/MaxPerf` vs `Batt/160MHz`).
- **InfoApp footer** updated from `2R:Exit` → `2L/R:Exit` to reflect that either double-click exits.
- **SSync volume hold triggering mute** — `SSyncApp::onLongRight()` made a no-op; mute/unmute is exclusively triggered by double-R click (`onBtnRightDouble`). Additionally, `ButtonInput` routes long-press callbacks into hold-repeat events only when hold-repeat is enabled, preventing a volume hold from also triggering the mute action.
- **Light theme accent visibility** — Changed light theme accent from `0x001F` (blue, invisible on white) to `0x01F4` (Deep Royal Navy/Teal).
- **Screen black-flash on app switch** — `onAppChange()` now calls `showThemedLoadingScreen()` before `load()`, replacing the abrupt black flash with a themed transition frame.
- **Launcher tile switch** — Uses `fillScreen(theme.bg)` instead of loading screen for instant-feel home navigation.

### Changed
- `ButtonInput::setClickMs` — 350 ms → **450 ms** for `DOWN` and `UP` buttons (wider double-click detection window).
- `AudioManager::getSourceColor()` / `drawStatusDot()` — Returns deep teal/plum/navy for SSync/Music/Video in light mode; dark crimson for error state in light mode.
- Light theme `flashHighlight()` uses black flash; dark theme uses white flash.

---

## [v1.1.0] — 2026-09-26

### Added
- **Safe Mode** — Three consecutive panics within the boot window trigger a reduced-feature safe-mode boot.
- **Web OTA update** endpoint (`/ota/upload`) — Push new firmware over Wi-Fi without USB.
- **REST API** endpoints: `/api/health`, `/api/status` — return JSON with battery voltage/percent, charging state, Wi-Fi RSSI, uptime, active app.
- **Screen wake on plug-in** — Device wakes the display when USB power is connected while the screen is off.
- **Screen wake during OTA** — Display turns on automatically during an OTA update to show progress, then can sleep normally afterwards.
- **`Wire.setTimeout(50)`** — Prevents I2C hangs from stalling the main loop when the ES8311 codec is unresponsive.
- **TWDT** (Task Watchdog Timer) — 15-second timeout catches infinite loops and hard hangs; triggers a clean reboot.
- **Hardware crash recovery** — Panic handler logs a crash reason to NVS before rebooting; consecutive-crash counter feeds into Safe Mode.
- **Battery icon polish** — Bolt indicator and fill level are theme-aware; icon blinks when critically low.
- **GalleryApp slideshow timer** — Configurable auto-advance interval (0 = manual) persisted in NVS.

### Fixed
- **I2C codec hang on cold boot** — `Wire.setTimeout(50)` prevents indefinite blocking.
- **PSRAM heap fragmentation** — Canvas allocations moved to PSRAM; `heap_caps_malloc` used for large buffers.

### Changed
- Status bar WiFi dot and battery icon rendering refactored into `PokoUI.h` helper methods.

---

## [v1.0.0] — 2026-09-20

### Added
- Initial public firmware release for **Waveshare ESP32-S3-LCD-0.85** (128×128 GC9107, ES8311, 8 MB Flash, 8 MB OPI PSRAM, WS2812 LEDs).
- **Launcher** — Horizontally scrolling carousel of app tiles with animated flash highlight.
- **ClockApp** — Full-screen analog + digital clock with date display.
- **SSyncApp** — Bluetooth SnapCast audio receiver with real-time volume bar, mute, source badge.
- **MusicApp** — Local file playback (LittleFS) with progress bar and album metadata display.
- **VideoApp** — MJPEG video player streamed from LittleFS.
- **GalleryApp** — Image slideshow from LittleFS or HTTP server with source badge.
- **PixelApp** — Real-time RGB LED color picker with live preview.
- **SettingsApp** — Scrollable settings list: brightness, volume limit, amp boost, slideshow timer, theme toggle, Safe Mode entry, power-off, reboot.
- **InfoApp** — Hardware system info: CPU freq, RAM, flash, battery voltage/percent, charging, Wi-Fi RSSI, IP, uptime, firmware version, build date.
- **Dual-button combo system** — L+R together: click = toggle audio/LED, double = jump to InfoApp, hold 2.5 s = clean reboot, hold 5 s = NVS wipe + reboot, hold 10 s = emergency reset.
- **Dark & Light themes** — Fully themed UI with `PokoTheme.h` structs; toggle in Settings.
- **AudioManager** — Exclusive audio session model: only one audio source active at a time; status dot on status bar.
- **Power management** — Battery low-voltage cutoff, auto-sleep, screen brightness control.
- **NVS persistence** — Volume, brightness, theme, gallery timer, SSync auto-connect saved across reboots.

