# Firmware regression checks

Run from the repository root:

```powershell
python tests/firmware/run_tests.py
python server/run_tests.py
python tests/firmware/stream_smoke.py
```

The host runner needs MSVC Build Tools, g++, or clang++. It uses the real installed
OneButton library, ahead of the optional test double in `stubs/`. Override its
location with `--onebutton PATH/TO/OneButton/src`.

The host cases exercise the production ButtonInput class with simulated GPIO
levels/time, the production AudioManager core with a worker thread and mutex,
the production VideoApp start/stop/navigation methods with transport doubles,
and the title marquee. The extraction scripts copy those methods directly from
the current firmware; they do not duplicate their implementation.

The streaming smoke test requires FFmpeg. It generates disposable clips under
`build/host-tests/streams`, receives real NAV1 JPEG/PCM packets on loopback,
and checks both audio/video and silent-video streams plus sender cleanup.
It never contacts or flashes the device.

Build firmware into a separate directory, preserving the existing binary:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" --build-path build/bugfix-compile --output-dir build/bugfix-output --jobs 4 .
```

For this fix, restart the media backend with the updated Python code as well as
installing the new firmware; silent-video transport and blocked-sender cleanup
are backend changes. Firmware v2 adds device-admin Digest authentication to
Web/OTA and a separate HTTPS media token. Pair before exercising those routes;
see `server/deploy/README.md` and `server/SECURE_LOCAL.md`.

## Checks on the physical device after flashing

- Info: each single L/R scrolls one row; two taps separated by about 400 ms exit.
  Hold R refreshes once without changing volume.
- Sleeping display: test L, R and PWR separately. The press wakes the display;
  holding or double-tapping the wake gesture does not navigate or shut down.
  Release and wait 450 ms; subsequent actions work normally.
- USB deep sleep: each of L/R/PWR wakes the board. Fully disconnected battery
  power is governed by the board's power-latch circuit, not firmware wake pins.
- Music/SSync/Video: holds still ramp volume. Info/Clock/Gallery/Settings/Home
  holds do not change it. Check SSync-to-Music/Video and return to SSync.
- Video: next/previous during playback, natural end, silent clips and a failed
  server connection. Failure should return to a retryable browser, not retain
  a frozen playback state.
- Long Gallery/Video titles scroll. Gallery waits for a full title pass before
  automatic fullscreen; short-title behavior and manual fullscreen still work.
- Check L+R click/double-click and existing reboot/driver-reset combinations.

Host and loopback tests cannot certify electrical button behavior, display
appearance, I2S timing, or hardware wake. Those require the device checks above.

## Microphone apps

The host suite additionally exercises production DSP with A440/tuning offsets,
notes from 41–1760 Hz, spectrum/octave/RMS calibration, all 12 roots for eight
chord templates, silence/noise rejection and auto-range hysteresis. App tests use
the production screens and DSP with bus/display doubles, covering mode/hold
controls, stale-input clearing, failure/retry and buffer cleanup. Driver tests
replace only I²C/I²S/GPIO, checking clock ownership, 48 kHz stereo configuration,
MIC1 extraction, non-blocking reads and failed-initialization cleanup. The actual
AudioManager core tests SSync → MIC → SSync, timeout refusal and stop-all cleanup.

After the user compiles and installs firmware, verify on the physical board:

- Open Analyzer from Home; all ten tiles remain reachable through eight dots.
- Speak/tap near MIC1: scope and FFT respond; cycle all four modes with `2R`.
  Hold each direction to adjust time, FFT length, octave spacing and meter response.
- Play an external A440 reference: Tuner should show A4 near 440 Hz and 0 cents.
  Check a quiet room, several lower/upper notes and clean major/minor/suspended/
  seventh/augmented chords. Stop the reference: readings must clear.
- Open each tool during SSync: playback suspends, PA stays off; `2L` resumes SSync.
  Repeat after Music/Video playback and after Wi-Fi reconnect/driver reset.
- Leave capture running beyond power deadlines; it must keep capturing while the
  display may sleep. Low-battery policy must still override as configured.
- Close/reopen repeatedly and inspect heap, audio handoff and absence of I²S errors.

Synthetic chord matches do not certify instrument/microphone recognition quality.
