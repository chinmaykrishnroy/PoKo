# PoKo Button Controls & Navigation Guide

This document details the physical button mapping, system-wide shortcuts, app navigation, and power management behaviors for **PoKo** (Waveshare ESP32-S3-LCD-0.85).

---

## 1. Physical Hardware Layout & Pinout

The PoKo hardware features three tactile buttons:

| Button Name | Pin / GPIO | Physical Function | Default Action |
|:---|:---|:---|:---|
| **`L`** (Left / Down) | **GPIO 0** (BOOT) | Navigation / Decrease / Previous | Move left/up, decrease volume |
| **`R`** (Right / Up) | **GPIO 4** | Navigation / Increase / Next | Move right/down, increase volume |
| **`PWR`** (Power) | **GPIO 5** | Power Management / Display Wake | Screen sleep toggle, power off |

> **Footer Convention**: All application screen footers strictly display **`L`** and **`R`** labels to match the physical hardware buttons.

---

## 2. System-Wide Button Combinations & Power Controls

These controls are active across the entire system regardless of the open application:

### Quick Actions & Combinations

| Action / Combo | Timing | Effect |
|:---|:---|:---|
| **`L + R` Hold** | **2.5 seconds** | **Clean System Reboot** (`ESP.restart()` with on-screen "REBOOTING..." alert) |
| **`L + R` Click** | Quick simultaneous press | **Audio Play/Pause / Mute** toggle |
| **`L + R` Double-Click** | Double press within 600ms | **Jump to Device Info** (`InfoApp` telemetry screen) |
| **`L + R` Long Hold** | **5.0 seconds** | **Hardware Driver Reset** (re-initializes I²C, ES8311 codec, GC9107 display, and WS2812 LEDs) |
| **`PWR` Click** | Single click | **Toggle Screen Sleep / Wake** (backlight + GC9107 low-power sleep) |
| **`PWR` Long Press** | **$\ge 2.0$ seconds** | **Graceful Power-Off** (flushes audio volume and settings to NVS, mutes speaker PA, shuts down Wi-Fi, turns off display and LEDs, and releases battery latch / enters deep sleep) |

---

## 3. Application Navigation & Controls

### 3.1 Home Launcher / App Carousel
- **`L`**: Previous App tile.
- **`R`**: Next App tile.
- **`2R` (Double Click `R`)**: Launch / open the selected application.

---

### 3.2 SSync (Snapcast Synchronized Audio)
- **`L`**: Decrease volume (1 dB / step; hold for continuous volume ramp down).
- **`R`**: Increase volume (1 dB / step; hold for continuous volume ramp up).
- **`2R`**: Toggle Mute / Pause or resume SSync audio stream.
- **`2L`**: Return to Launcher.

---

### 3.3 Music Player
- **`L`**: Previous track (or restart current song).
- **`R`**: Next track.
- **`2R`**: Play / Pause toggle.
- **`2L`**: Exit to Launcher.
- **Hold `L` / Hold `R`**: Smooth volume decrease / increase.

---

### 3.4 Video Player (MPEG1 / AVI)
- **`L`**: Previous video file.
- **`R`**: Next video file.
- **`2R`**: Play / Pause toggle.
- **`2L`**: Exit to Launcher.
- **Hold `L` / Hold `R`**: Volume control during playback.

---

### 3.5 Photo Gallery & Slideshow
- **`L`**: Previous image.
- **`R`**: Next image.
- **`2L`**: Exit to Launcher.

---

### 3.6 Clock App
- **`L`**: Cycle Clock Display Mode:
  - Minimal Digital
  - Split Flip-Clock
  - Retro Nixie Tube
  - Analog Dial Face
- **`R`**: Cycle Color Palette Theme (Accent, Ember Orange, Matrix Neon, Monochrome, Cyber Cyan).
- **`2R`**: Exit to Launcher.

---

### 3.7 WS2812 LED Lighting (`PixelApp`)
- **`L`**: Move to previous setting row.
- **`R`**: Move to next setting row.
- **`2R`**: Cycle effect pattern (Rainbow, Static, Pulse, Audio Reactive, Off).
- **Hold `L` / Hold `R`**: Adjust LED brightness and animation speed.

---

### 3.8 System Settings (`SettingsApp`)
- **Footer**: `L:Prv  R:Nxt  2R:Set`
- **`L`**: Move selection up to previous setting (loops around).
- **`R`**: Move selection down to next setting (loops around).
- **`2R`**: Cycle value / toggle setting / execute action:
  1. **Theme**: Dark $\leftrightarrow$ Light
  2. **Master Vol**: 20% $\rightarrow$ 40% $\rightarrow$ 60% $\rightarrow$ 80% $\rightarrow$ 100%
  3. **Brightness**: 25% $\rightarrow$ 50% $\rightarrow$ 75% $\rightarrow$ 100%
  4. **Amp Boost**: +0 dB $\rightarrow$ +3 dB $\rightarrow$ +6 dB $\rightarrow$ +9 dB
  5. **Dim Timeout**: Off $\rightarrow$ 5s $\rightarrow$ 10s $\rightarrow$ 15s $\rightarrow$ 30s $\rightarrow$ 60s
  6. **Sleep Timeout**: Off $\rightarrow$ 15s $\rightarrow$ 30s $\rightarrow$ 1m $\rightarrow$ 2m $\rightarrow$ 5m
  7. **Auto-Off**: Never $\rightarrow$ 5m $\rightarrow$ 10m $\rightarrow$ 15m $\rightarrow$ 30m
  8. **Ambient Clock**: On $\leftrightarrow$ Off (keeps clock visible dimmed rather than sleeping screen)
  9. **WiFi Sleep**: Auto $\leftrightarrow$ Off
  10. **USB Mode**: `MaxPerf` $\leftrightarrow$ `Managed`
  11. **Slide Timer**: Off $\rightarrow$ 3s $\rightarrow$ 5s $\rightarrow$ 10s $\rightarrow$ 15s $\rightarrow$ 30s $\rightarrow$ 60s
  12. **SSync Auto**: On $\leftrightarrow$ Off (auto-connects Snapcast on boot/Wi-Fi)
  13. **LED Bright**: Off $\rightarrow$ 20% $\rightarrow$ 50% $\rightarrow$ 100%
  14. **Reset Drivers**: Execute emergency re-initialization of peripheral drivers
  15. **Power Off**: Orderly shutdown sequence
  16. **Reboot**: Clean device reboot

---

### 3.9 Hardware & System Info (`InfoApp`)
- **Footer**: `L:Prv  R:Nxt  2L/R:Exit`
- **`L`**: Scroll list up (continuous bidirectional wrap-around loop).
- **`R`**: Scroll list down (continuous bidirectional wrap-around loop).
- **`2L`** or **`2R`**: Exit to Launcher (450ms double-click window for reliability).
- **Hold `R`**: Force refresh of all live hardware metrics.

---

## 4. Power Management & USB Dynamic Operation

### 4.1 Battery Detection & Header Icon
- **No Battery Present** ($V < 2.50\text{V}$):
  - The battery icon in the top header is **completely hidden**.
  - Battery low-voltage cutoff and battery auto-off timeouts are automatically bypassed.
  - The device operates as a standing desktop terminal on USB power.
- **Battery Present** ($V \ge 2.50\text{V}$):
  - Dynamic battery icon sits adjacent to the status bar clock.
  - **Charging**: Battery icon blinks in bright green with a charging bolt indicator.
  - **Discharging**: Fill bar dynamically transitions from green ($>50\%$) through yellow down to red ($\le 15\%$).
  - Low-voltage battery protection automatically powers down the system if cell drops below $3.25\text{V}$ sustained for 15 seconds.

### 4.2 USB Performance Modes
Configurable in Settings $\rightarrow$ `USB Mode`:
- **`MaxPerf` (Default on USB)**:
  - Wi-Fi 802.11 modem sleep is **disabled** (`WiFi.setSleep(false)`), providing minimum socket latency and maximum bandwidth for SSync audio, Web UI streaming, and OTA updates.
  - CPU operates at maximum 240 MHz dual-core clock.
  - Auto-off deep sleep is bypassed.
- **`Managed`**:
  - Dynamically throttles radio into modem sleep when idle even when connected to USB to reduce thermal dissipation and power draw.
