# Acoustic validation: v2.5.0

Firmware commit: `19324123c1d7788dde5ce80a09575f432df97108`.
Hardware checks performed on 2026-10-31 with the Waveshare ESP32-S3-LCD-0.85.

## Passed

- GitHub Actions built the real firmware using Arduino ESP32 core 3.3.12 and
  `esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB`.
  All 157 firmware host regressions and 88 backend tests passed.
- The release binary's SHA-256 was verified before OTA, and the device reported
  v2.5.0 after reboot.
- Music remained playing through both tools and all six modes. The observer
  used the actual 44.1 kHz stream clock, measured approximately 44,239 and
  44,126 frames/s, and reported zero observer overruns.
- Audible SSync remained playing through both tools and all six modes. The
  observer used 48 kHz, measured approximately 47,987 and 47,992 frames/s,
  and reported zero observer overruns. The verifier checks audible playback
  (`playing`), rather than treating audio ownership as proof of playback.
- Dedicated microphone continuity checks passed all six modes at approximately
  48,034 and 47,937 frames/s. The speaker amplifier stayed off. Sixteen reopen
  cycles left free heap stable at approximately 189,200 bytes.

Playback checks establish device state and PCM continuity; they are not a
substitute for listening for artifacts.

## Unresolved: microphone chord accuracy

The microphone did **not** reliably identify the reference C-major guitar loop.
A 35-sample live telemetry check produced 28 blank readings, two `Caug`, four
`Emaj`, and one `F#maj`. A preceding check also observed `E7`. Gain was 30 dB;
RMS varied from approximately -70 to -31 dBFS over the loop. This is a confirmed
accuracy failure, not an acceptance pass or proof that gain alone is responsible.

The same 35-sample polling session observed two microphone RX overruns. The
separate steady-capture checks had no overrun increase. The cause of the
intermittent overruns remains unconfirmed.

An authorized desktop HyperX recording replayed through the production DSP
improved from zero correct C-major labels to 29 correct labels and 18 blank
readings among the 47 frames above -50 dBFS, with no wrong displayed chords in
those frames. That result did **not** transfer to the board microphone. The
recordings are private, ignored build artifacts and are not included here.

Further chord work needs spectral evidence from the board's failing input and
an automated reproducer. Avoid tuning thresholds solely against the desktop
recording or claiming that host regressions prove acoustic accuracy.

The remote Play action while Tuner already owns the microphone has host
regression coverage; its separate hardware handoff check was not completed in
this session. Microphone-to-SSync automatic resumption also still needs a
dedicated live transition check.

After these checks, the original master volume (100), Home foreground, SSync
service, and idle timers (90/120/300/600 seconds for Wi-Fi hibernate, light sleep,
deep sleep, and auto-off) were restored.
