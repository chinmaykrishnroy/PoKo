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

The 30 host cases exercise the production ButtonInput class with simulated GPIO
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
are backend changes. Web/OTA authentication is intentionally unchanged.

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
