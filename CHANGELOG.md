# Changelog

All notable changes to **Poko** firmware are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## Unreleased

- Server: preserve the origin of browser login/logout form submissions with
  `Referrer-Policy: same-origin`. The previous `no-referrer` policy produced
  `Origin: null` for native forms and incorrectly rejected legitimate logins.
  Null and foreign origins remain blocked. Firmware is unchanged.

## [v2.1.2] — 2026-10-28

- Includes the session/password features below, with Linux/Windows host tests.
- Bound queued device-authentication waits to each action's timeout budget.
- Treat remembered-device sessions awaiting clock sync as a temporary retry,
  rather than prompting for a password. Saved-password pages support L/R paging.
- Earlier v2.1 candidates were superseded before publication while isolating
  test configuration and fixing a missing header in the Linux test stub.

## [v2.1.1 candidate] — 2026-10-28

- Includes the session/password features below. The v2.1.0 candidate was
  superseded after CI identified a test depending on an ignored local config;
  the regression now creates and uses its own isolated configuration.

## [v2.1.0 candidate] — 2026-10-28

- Replaced browser HTTP-auth prompts with login forms, HttpOnly session cookies,
  Remember me, and logout on both dashboards. API credentials remain separate.
- Serialized device Digest exchanges after reproducing parallel shared-nonce
  failures. Upstream device authentication errors no longer trigger server login.
- Added physical viewing/paging of the device's existing saved password, with
  reset retained as a separate confirmed action.
- Added server Web UI passphrase changes, private persisted scrypt verifiers,
  operator recovery, and optional memorable six-word credential generation.
- The wording-only v2.0.3 candidate was superseded before publication/flashing.

## [v2.0.3 candidate] — 2026-10-28

- Use “Server” in security messages so pairing guidance applies to any host.
  Shortened the on-device message to fit the 128-pixel screen.

## [v2.0.2] — 2026-10-28

- Added device Web UI password changes with current-password verification and
  confirmation, password-manager fields, and no browser credential storage.
- Added Settings → Security with device address, pairing status, and confirmed
  password recovery displaying a generated password only on the device.
- Preserve existing Pi media controls through a separate restricted integration
  credential when the human login changes; deny it administration and OTA.
- Added regressions for credential migration, access restrictions, validation,
  failed preference writes, and rejection of password rotation via setup.

## [v2.0.1] — 2026-10-28

Includes the authenticated local/cloud architecture and review fixes below.
The v2.0.0 candidate was superseded before publication or device flashing.

- Unified the remaining Music browser-unload and source-handoff cleanup paths.
  Shutdown timeouts retain ownership/protection and retry before releasing I2S.
- Exhausted or emptied Music/Video catalogs now stop cleanly at end-of-stream.
- Added ownership-handoff and shutdown-retry regression cases.

## [v2.0.0 candidate] — 2026-10-28

### Security and compatibility
- Separate browser-admin password, restricted device media token, and ESP32
  device-admin password. Device credentials cannot administer the media server.
- Verified HTTPS for Music, Video, and Gallery, using the built-in CA bundle
  or a provisioned private CA for secure local servers. Tokens are refused on
  plaintext HTTP and are never compiled into release images.
- Device control and web OTA require Digest authentication. Upload callbacks
  authenticate before writes; ArduinoOTA stays disabled until provisioning.
  First pairing is available on the trusted LAN for five minutes after boot.
- Added an isolated, restartable Pi service, private credential provisioning,
  direct local TLS support, bounded HTTP concurrency, and cross-origin checks.
- Existing unauthenticated device clients need provisioning after upgrading.
  Raw PCM/MJPEG transports remain restricted to the trusted LAN.

### Fixed
- Music terminal errors, start timeouts, and exhausted auto-advance release
  ownership only after worker shutdown is confirmed; timed-out shutdown retries.
- Music progress follows a monotonic anchor through blocked main-loop updates.
- Silent Video no longer reports audible audio; the A/V clock accounts for
  confirmed I2S writes. Health telemetry includes frame drops and audio underruns.
- Video validates asynchronous acceptance and reconciles operation/request IDs,
  failed operations, cancellations, and mismatched generations.
- Playback control failures return non-2xx statuses. Library pages are bounded
  and thumbnails are cached by path, modification time, size and dimensions,
  with bounded background producers and bounded disk retention.
- Mutable device gallery images no longer retain stale browser thumbnails.
- Corrected first-USB-flash instructions and the L+R Solid/Off documentation.
- Removed the unused parallel AppManager and dependency-container scaffolding.
- Corrected CI's nonexistent Arduino setup action alias to its published commit.

### Policy
- Display-on Wi-Fi hibernation remains intentional; the dot stays static pink.
  Active media prevents hibernation, and light/deep sleep require display-off.

---

## [v1.6.0] — 2026-10-06

### Added
- Configurable Wi-Fi hibernation, light sleep, deep sleep, idle CPU frequency,
  and loop-yield controls in the device Settings app and Web API.
- Power transition telemetry for display, radio, light/deep sleep, auto-off,
  power locks, protected media, and separate user/media idle clocks.
- Filtered voltage-based battery percentage with directional slew limiting,
  plug-state settling, and low-voltage hysteresis.

### Changed
- Audio hardware now initializes lazily and fully powers down the PA, I2S,
  MCLK, and ES8311 after playback sessions end.
- Idle deep sleep is documented and handled as a reboot with RTC-retained
  pending power-off state. Light-sleep timer wakes target the next deadline.
- CHG_STAT remains charging telemetry and is no longer presented as reliable
  USB/VBUS presence. Detectable clean charge-only starts remain logically off.

### Fixed
- Continuous audible SSync, Music, and Video maintain their power locks and
  cannot be interrupted by Wi-Fi hibernation, light/deep sleep, codec shutdown,
  or normal auto-off. Low-battery protection remains authoritative.
- A transient playback-state sample can no longer expose already-expired user
  idle deadlines and shut down active media.
- Network app actions after Wi-Fi hibernation wait for usable connectivity.
- EXT1 light-sleep wake restores GPIO 0, 4, and 5 from RTC mode before normal
  button handling resumes.
- The hibernated Wi-Fi indicator is static medium pink without forcing 200 ms
  launcher redraws.

### Tested
- Added long-duration protected-media, playback-stop progression, low-battery,
  light-sleep deadline, network-wake, audio activity, and battery slew tests.
- Verified the user-compiled image by OTA on physical hardware at
  `192.168.0.4`; the device rebooted and exposed the new power telemetry.

---

## [v1.5.0] — 2026-10-02

### Changed
- Replaced `volatile` inter-task state with atomics, mutex-protected metadata,
  queues, and short critical-section snapshots appropriate to each data path.
- Audio, video, and Snapcast worker task handles are now owned by the control
  task, with confirmed shutdown required before shared resources are released.
- CI now rejects first-party firmware code that reintroduces `volatile` as a
  synchronization mechanism.

### Fixed
- Snapcast endpoint, codec, clock offset, playback anchor, and 64-bit sample
  state can no longer be read while another core partially updates them.
- Simultaneous synchronized audio/video disconnects no longer race while
  publishing connection state.
- Audio and synchronized-video shutdown timeouts preserve live buffers for a
  safe retry instead of risking use-after-free.
- Audio decoder and UI canvas allocation failures now degrade safely instead
  of dereferencing null objects and rebooting the device.

---

## [v1.4.0] — 2026-10-01

### Changed
- Completed the declaration-only firmware header migration. Hardware drivers,
  app controllers, media transports, input, power, themes, state helpers, and
  embedded Web UIs now have matching `.cpp` implementations.
- CI now prevents first-party modules from regaining header implementations and
  rejects unclassified new header-only firmware modules.

### Fixed
- Periodic battery voltage sampling no longer bypasses charging-pin debounce or
  swallows the USB plug-in event used to wake the display.
- Display creation is idempotent, and display initialization/driver reset now
  reject a null display instead of dereferencing it.

## [v1.3.4] — 2026-09-30

### Fixed
- Video and image catalog titles now use their filenames, preventing generic
  embedded audio-track labels such as `Stereo` from replacing media names.
- Embedded Music and Video playback now targets the device address observed on
  the playback request, so DHCP address changes no longer break server-pushed
  audio or synchronized video while Gallery continues to work.
- The L+R click now toggles the Pixel engine between Solid and Off while
  preserving its saved RGB color, brightness, and LED target.

---

## [v1.3.3] — 2026-09-30

### Fixed
- Music, Video, and Gallery now distinguish an unreachable server from missing
  media and server-side failures instead of always showing `Start PoKo Server`.
- A stale music, video, or image ID caused by a changed media path now refreshes
  the catalog and selects the closest available item automatically.
- When refreshed media is unavailable, the device reports `Media Not Found`;
  it reserves the offline prompt for transport failures.

### Tested
- Added firmware regression cases for successful stale-video recovery and an
  empty refreshed catalog.

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
- Gallery, Music, and Video now bound failed server connections so a backend
  shutdown cannot leave the device on a loading screen until the watchdog resets.
- Music and Video reuse the existing offline/retry screen after a stream drops
  and suppress stale cached artwork or thumbnails while the server is offline.

### Added
- GitHub Actions CI for backend tests, firmware host regressions, real FFmpeg
  loopback streaming, and a pinned ESP32-S3 firmware build.
- Semantic-version tags publish the OTA application binary and SHA-256 checksum
  as GitHub Release assets after every validation gate passes.
- A documented `server/config.example.yml` template with fixed 128×128 output
  and high-detail MJPEG defaults.

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
- **`getAppName()` helper** in `PoKo.ino` — maps `AppState` enum to a human-readable string used by the loading screen.
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

