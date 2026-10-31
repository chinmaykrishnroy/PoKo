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

## Earlier intermediate detector (historical, superseded)

The measurements below describe an intermediate FFT/chroma refinement, before
the notes-first implementation. They are retained as a baseline, **not results
for the current source**. See [the current architecture](tuner-analysis.md).

The saved default-input recording reproduced the failure through the production
DSP: 22 wrong displayed seventh readings and only five correct readings among
52 frames above -50 dBFS. Inspection found two distinct causes: broad low-band
noise received excessive chroma weight, while bright higher harmonics could be
treated as independent B notes. Conversely, the detector could swallow a real
E5 in a wide C3/G4/E5 voicing as a bass harmonic.

The follow-up weights local spectral contrast, bounds that weighting so nearby
real notes survive, and requires stronger harmonic corroboration for grouping.
Chord mode also estimates a coherent common tuning offset across three pitch
classes. A440 note tuning is unchanged.

| Same saved recording, same -50 dBFS threshold | Correct before | Wrong before | Correct after | Wrong after | Uncertain after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows default input | 5 | 22 | 36 | 0 | 16 |
| HyperX | 29 | 0 | 33 | 0 | 14 |

No wrong displayed chord occurred in any of the 119 replayed analysis frames
of either clip, including the quieter frames. These are two recordings of one
reference loop, not a general accuracy benchmark. The 69%/70% correct fractions
above the threshold do not meet the proposed 90% engineering goal; abstentions
are deliberately reported separately from correct labels.

The full host suite passed 165 regressions. The final DSP sweep also passed all
eight supported chord types at all 12 roots in the wide voicing, and at both
-35 and +35 cents. Single-note/noise rejection and genuine seventh tests remain
in the suite. An additional full-scale PCM observer test verifies that neither
Music nor SSync observation invokes microphone gain or changes audio ownership.

An additional assessment reproduced the C3/E5/G6 failure. Both upper peaks were
being assigned to the low C using the broad harmonic tolerance. A bounded second
grouping now resolves that clean equal-tempered voicing at all 12 transpositions,
while an exact single C with fifth/twelfth harmonics still produces no chord.
The retry only runs when normal grouping fails on 3–8 peaks with high tonality;
it does not replace the normal guitar path or implement general multi-pitch
estimation.

Best/runner-up ranking and a heuristic near-tie margin now distinguish ambiguous
matches from insufficient evidence and no chord. Aliases with identical pitch
sets are excluded from the runner-up comparison. On the HyperX replay this
conservatively replaces one previously correct frame with an abstention; neither
saved recording gains any wrong label. A retained uncertain UI label is amber
and is not shown with a fresh match bar.

A synthetic C → G → Am → Cmaj7 progression produces first correct displayed
labels in 300/200/300/200 ms respectively, and clears after a following 700 ms
silence interval. This is a deterministic host regression, not a measurement
of acoustic onset latency or a benchmark of real performances. Onset-sensitive
feature smoothing, a diverse labeled real-audio corpus, independent bass/slash
recognition, and broader chord vocabulary remain separate future work.

Replay can be repeated with `tests/firmware/replay_chords.py`; its source and
the production chord-mode harness are available without including the private
recordings. The same command fails against the old detector and passes against
the updated source.

These changes have **not** yet been compiled as firmware or checked on the
physical microphone. Actual DSP duration, RX overruns and chord accuracy still
need verification after the user compiles and uploads the updated firmware.
