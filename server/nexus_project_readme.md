# Nexus Project README

Nexus is a tiny media computer built around the **Seeed Studio XIAO ESP32S3**. The goal is deliberately ambitious: make a 240x240 display feel like a small, focused smart device with apps, media browsing, camera capture, notes, clock, settings, and a Python backend that prepares heavy work for the ESP32.

This file is also a test document for the Nexus Notes reader. It includes headings, paragraphs, quotes, lists, numbered steps, inline styles, links, image syntax, horizontal rules, and code blocks.

## Hardware

The current target is the XIAO ESP32S3 with PSRAM enabled. The device is designed around these attached modules:

- 1.3 inch 240x240 TFT display.
- Rotary encoder with switch for navigation.
- I2S amplifier and speaker for audio playback.
- XIAO Sense camera module for still capture and preview.
- Built-in or attached microphone for future recorder apps.
- WiFi connection to a Python backend server.

The rotary encoder is the main physical control. Clockwise and anticlockwise movement changes focus, single click activates, double click returns, long press performs secondary actions, and very long press can be used for recovery actions.

## Main UI

The home screen is intentionally app-like. It has pages of solid-color app tiles with simple icons. Page one contains the most important apps:

1. System Info.
2. Music.
3. Video.
4. Gallery.

Page two contains utility apps:

1. Clock.
2. Settings.
3. Camera.
4. Notes.

The heading shows **Nexus**, the current time, and WiFi strength. Only the changing status area should refresh so the launcher does not flicker during normal updates.

> Design rule: the ESP32 should feel responsive even when the backend is indexing, fetching thumbnails, or preparing streams.

## Backend

The backend is a Python server in the `server` folder. It reads folders from `server/config.yml`, indexes media into SQLite, and exposes compact JSON APIs for the ESP32.

The backend reads:

- Audio files.
- Video files.
- Image files.
- Text and Markdown files.

It writes future device uploads into a configured write folder. Camera captures are saved under a `camera` folder inside that write path.

### Backend Responsibilities

The ESP32 should not decode large media formats directly. The backend does the expensive work:

- Probe metadata with FFmpeg and FFprobe.
- Extract audio duration, artist, title, and thumbnails.
- Convert audio into a stream format the ESP32 can play smoothly.
- Convert video into display-sized JPEG frames.
- Decide whether a video needs the Video Player or Graphics Player.
- Return paged lists of five items for low memory browsing.
- Return thumbnails lazily so browsing stays snappy.

## Music App

The Music app opens to a small menu with **Browse** and **Settings**. Browse fetches pages from the backend. Each list page contains five songs.

Each song tile shows:

- A thumbnail or fallback audio icon.
- Song title.
- Artist name.

During playback the player shows the thumbnail, title, artist, elapsed time, total duration, and progress. Rotary movement controls volume. Single click toggles pause. Double click returns to the list. Long press seeks forward. Triple click seeks backward.

The best audio behavior depends on predictable streams. The backend currently sends MP3 audio to the ESP32 I2S audio player.

## Video App

The Video app uses the same Browse and Settings pattern as music. The list shows videos with thumbnails and relative paths, which helps identify episodes and folders.

When a selected video has audio, the backend should use the synced Video Player. When a video has no audio stream, it should use the Graphics Player.

The synced Video Player uses audio as the master clock. Video frames may drop if they are late, but audio should continue. This is better than letting audio pile up after video freezes.

## Graphics Player

Graphics Player is the older MJPEG-style player. It is useful for:

- Silent videos.
- Motion images.
- Simple visual streams.
- Debugging display performance.

It does not try to provide perfect audio/video sync. That is the job of the Video Player.

## Camera App

The Camera app has **Launch** and **Settings**. Launch opens a selfie preview. Single click captures a photo. Double click returns to the Camera menu. Long press opens the camera gallery.

Capture behavior should be non-blocking from the user perspective:

1. Pause preview briefly.
2. Capture one JPEG frame.
3. Save the image locally.
4. Resume preview.
5. Upload to the backend in the background when the server is reachable.

Camera settings include frame size, JPEG quality, selfie flip, vertical flip, brightness, contrast, and local save count.

> The local save count exists because the ESP32 has limited flash. Keeping only a few recent photos prevents storage from quietly filling up.

## Notes App

The Notes app reads `.md`, `.markdown`, and `.txt` files from backend-indexed folders. Text files are rendered plainly with word wrap. Markdown files use a compact renderer tuned for the 240x240 color display.

Supported Markdown features include:

- `#`, `##`, and `###` headings.
- Paragraphs with word wrap.
- **Bold text**.
- *Italic text*.
- ***Bold italic text***.
- `Inline code`.
- Bullet lists.
- Numbered lists.
- Block quotes.
- Horizontal rules.
- Links, rendered as readable text.
- Image syntax, rendered as an image placeholder.
- Fenced code blocks.

Tables are intentionally not supported yet. They need a different layout model to work well on a tiny square screen.

### Example Formatting

This paragraph includes **bold**, *italic*, ***bold italic***, and `inline code`. A link such as [OpenAI](https://openai.com) should show the link text rather than the raw URL. An image like ![Nexus device](nexus.jpg) should become a compact image placeholder.

> A good reader on a small display is not a desktop Markdown viewer. It should be readable, stable, and predictable while scrolling.

---

### Example List

- Keep the UI responsive.
- Keep memory pressure low.
- Use the backend for heavy transforms.
- Prefer lazy thumbnails.
- Avoid blocking the main loop.

### Example Numbered Flow

1. Open Notes.
2. Choose Browse.
3. Select this README.
4. Rotate the encoder to scroll.
5. Double click to return to the list.

### Example Code Block

```cpp
void capturePhoto() {
  camera.pausePreview();
  camera.captureJpeg();
  camera.resumePreview();
  uploader.queueLatestPhoto();
}
```

## System Info

System Info is the place for facts about the device. Useful fields include uptime, WiFi status, IP address, backend URL, active app, free heap, PSRAM availability, display state, hotplug state, and media plugin status.

The uptime format should be compact. It can show `hh:mm:ss` for short sessions and `dd hh:mm:ss` once the device has been running for days.

## Settings

Settings should remain small enough to operate with the rotary encoder. Current useful settings include:

- Screen brightness.
- Light or dark theme.
- Default volume.
- Encoder direction.
- Cursor inactivity timeout.
- Hot recovery enable.
- Display replug delay.

App-specific settings belong inside each app. For example, Camera owns camera quality and Notes owns reading colors and scroll amount.

## API Expectations

The device web API should report the real effective state. If music is playing from the UI, `/api/info` should not only say `menu`. It should expose both the shell UI state and the effective player state.

Example shape:

```json
{
  "activeApp": "audio_player",
  "shellApp": "music",
  "plugins": {
    "audio": { "loaded": true, "connected": true },
    "graphics": { "loaded": false, "connected": false },
    "video": { "loaded": false, "connected": false }
  }
}
```

## Reliability Rules

The ESP32 main loop must stay free to:

- Handle rotary input.
- Serve HTTP API requests.
- Keep OTA reachable.
- Feed watchdogs.
- Redraw small UI areas.
- Stop or switch apps cleanly.

Any slow operation should be pushed to a worker task or the Python backend. This includes thumbnail extraction, media conversion, camera upload, and indexing.

## Future Apps

Nexus has room for more apps:

- Voice recorder.
- Backend file inbox.
- Weather.
- Timers.
- Pomodoro.
- Local photo gallery.
- Remote dashboard.
- Tiny ebook reader.
- WiFi setup status.
- Diagnostics and logs.

The best apps are the ones that respect the display. They should be glanceable, tactile, and calm.

## Final Notes

Nexus is not trying to be a phone. It is a tiny dedicated companion device. Its strength is not raw power; its strength is a thoughtful split between firmware and backend.

The ESP32 handles interaction, display, audio output, camera preview, and streaming endpoints. The Python backend handles indexing, metadata, thumbnails, media conversion, and long-running jobs.

When those two halves cooperate, the little 240x240 screen can feel much bigger than it is.
