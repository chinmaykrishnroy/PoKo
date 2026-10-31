# Tuner: notes-first analysis

This describes the v2.6.0 source and its host validation before the release CI
build. It is separate from the [v2.5.0 hardware report](acoustic-validation-v2.5.0.md).
New physical-microphone accuracy and DSP timing still require board validation.

## Implemented pipeline

1. **Two real time resolutions.** The fast FFT uses 8192 input samples. Chord
   mode adds a 63-tap anti-alias FIR, decimation by four and a 4096-point bass
   FFT, refreshed at most every 200 ms of input. At 48 kHz the windows are
   170.7/341.3 ms and the bin spacings 5.86/2.93 Hz. The bass spectrum supplies
   peaks below 500 Hz once its history is valid; the fast band can identify an
   onset before then. Labels and windows follow the actual input clock, including
   22.05/44.1 kHz playback. Interpolation is a sub-bin estimate, not additional
   independent frequency resolution. Analyzer's separate Bass view still uses
   its longer 8192-point decimated FFT. Note mode does not run the bass filter.
2. **Independent fundamentals before chords.** `NoteEstimator` compares MIDI
   36–107 (C2–B7), up to 16 harmonics each, against bounded spectral peaks.
   Local contrast reduces broad noise. Candidate frequency, limited string
   inharmonicity and a bounded spectral envelope are fitted before eight passes
   of nonnegative coordinate descent against shared residual evidence. Refitting
   can withdraw an earlier interpretation; peaks are not permanently assigned
   to the first plausible parent. Observed versus inferred hypotheses compete;
   several notes' shared low divisor cannot erase their supported families.
   Missing fundamentals require corroborated coprime partials. Consistent bright
   upper families can survive missing low partials, while a coherent single
   harmonic series remains one note. Output includes MIDI, estimated frequency,
   normalized salience, a heuristic evidence score, and observed/current flags.
3. **Tuning.** A coherent offset across at least three independent pitch classes
   can refine chord-only matching. The note meter continues to measure cents
   against the saved A4 reference. Manual calibration is 400–480 Hz in 0.1 Hz
   steps; default 440.0 Hz. Automatic compensation never rewrites that preference.
4. **Chord decisions.** Sixteen templates rank required independent-note support,
   coverage and unexplained note energy. Corroborated quiet voices can contribute
   with less amplitude than isolated peaks. Required tones need current evidence;
   old tracked notes cannot manufacture a new chord. The best and a runner-up
   with a different pitch set are retained. A close score gap yields `ambiguous`;
   insufficient evidence and no chord are separate decisions. Scores and displayed
   match percentages are heuristics, not calibrated probabilities.
5. **Temporal evidence.** Independent-note features accumulate with a fast attack
   and profile-specific release. Musical flux and RMS rise detect onsets; a new
   attack refreshes evidence instead of carrying the previous chord into it.
   Guitar release is slower than Keyboard release. Label confirmations distinguish
   initial/onset matches, same-root quality changes and root changes during a
   decay. An uncertain display hold is bounded to 450 ms and shown in amber.
   Digital silence and known source/rate/capture discontinuities clear history.
6. **Independent bass.** The lowest sufficiently supported current fundamental
   is tracked separately from chord root. An arbitrary low spectral peak or held
   note does not directly become bass. This enables C/E, C/G and Am/C. Root and
   bass need not be the same pitch class. A supported missing-fundamental single
   note can provide bass, but an inferred polyphonic common divisor cannot.
7. **Vocabulary and profiles.** Qualities are major, minor, sus2, sus4, dominant 7,
   maj7, m7, augmented, diminished, dim7, half-diminished (`m7b5`), 6, m6, add9,
   madd9 and power (`5`). Guitar/Keyboard profiles change harmonic priors,
   envelope adaptation, temporal release and the six/eight-note output limits.
   Equivalent pitch sets (C6/Am7, Csus2/Gsus4, symmetric chords) cannot determine
   musical intention uniquely; bass is a naming tie-break. A power chord has no
   third and therefore does not establish major/minor quality.

## Controls and persistence

Analyzer and Tuner remain **two separate launcher apps**. `2R` changes modes,
`2L` returns home; existing gain/time-window controls remain. Chord mode shows
estimated note names below the main chord label. Long labels reduce font size
instead of running outside the 128×128 display.

Device Settings adds **Tuner A4** and **Tuner Type**. Hold L/R on A4 changes
0.1 Hz; Enter advances 1 Hz and wraps after 480 Hz. The device Web UI offers the
same calibration and Guitar/Keyboard selector. Invalid/nonfinite values are
rejected. Preferences are saved in the existing Tuner namespace; writes defer
while an app is loaded or playback is active, then flush when idle or during
orderly shutdown. Settings do not change playback volume, PCM samples or I²S
ownership.

The device admin-authenticated API is:

```text
GET /api/acoustic/settings
POST /api/acoustic/settings
Content-Type: application/x-www-form-urlencoded
reference_hz=442.0&profile=keyboard
```

Either field can be updated independently. Successful responses return canonical
`reference_hz` and `profile`; invalid input returns 400, unsupported methods 405.
These are device settings, not a change to the media server configuration.

## Runtime and observability

`NoteEstimator` allocates no memory while processing. Hypotheses and work arrays
are bounded members; the bass FFT reuses the fast FFT's workspace, caching its
plot/band results first. DSP/canvas allocations occur only when opening an app
and are released on exit; allocation failure follows the existing error screen.

Active Tuner chord analysis takes `POWER_LOCK_DSP` for 240 MHz. This CPU-only
lease does not request a networking/display lock and is released on digital
silence, stale input, mode change, capture failure or unload. Existing microphone/media power
protection and low-battery overrides continue independently. Passive playback
observation discards analysis copies on overflow, never playback data.

Authenticated `GET /api/acoustic` adds `notes`, `bass_midi`, `bass_ready`,
`bass_bin_hz`, `bass_window_ms`, `reference_hz`, `profile`, `onset` and
`analysis_max_ms`. Existing diagnostics include current analysis duration,
source/clock, capture age, overruns, best/runner-up scores, decision and margin.
`notes[].current=false` distinguishes decaying tracked evidence from a fresh
observation. `observed=false` means the fundamental is inferred from partials.
`evidence_score` is a support heuristic, not a probability. No raw microphone
recording is served by this endpoint.

### Full scale and the spectrum view

The logarithmic view is called **FFT Spectrum**. Its frequency axis remains
logarithmic and its amplitude scale remains dBFS; sample peak and FFT component
levels are different measurements.

Both microphone and PCM show amber **FULL** when an original 16-bit sample
reaches a rail. Reaching **0 dBFS alone does not prove clipping**. Integer samples
cannot retain excursions beyond their ceiling; PoKo does not claim to measure
upstream analog distortion, reconstructed intersample peaks or their excess.
The sample-peak meter uses recent original samples (up to the 8192-sample raw
history), before analysis DC subtraction or bass filtering. An asymmetric
centered waveform or FIR overshoot cannot manufacture >0 dBFS or a FULL flag. Microphone gain can still
back off to preserve headroom. Playback gain and volume never change.
`sample_at_full_scale` is the explicit telemetry name; `near_full_scale` remains
an alias for older clients. See the [PCM sample ranges](https://learn.microsoft.com/en-us/windows/win32/multimedia/devices-and-data-types)
and [integer versus floating sample formats](https://manual.audacityteam.org/man/sample_format_bit_depth.html).

## Validation and limits

Run the production-code host regressions and browser checks:

```powershell
python tests/firmware/run_tests.py
node --test tests/server_ui.test.cjs tests/device_tuner_ui.test.cjs
python tests/check_header_boundaries.py
python tests/check_concurrency_contracts.py
python tests/check_boot_startup.py
```

The DSP suite exercises all 16 qualities at 12 roots under both profiles,
±35-cent chords, wide/extreme voicings, single harmonic-series rejection,
missing fundamentals, true bass resolution, multiple playback clocks, inversions,
feature/label transitions, silence, and a bounded object-memory budget. App tests
exercise calibration persistence and deferred writes, source preservation,
CPU-lease cleanup (including stalled input), full-scale semantics and the rendered labels. Host timings
are not ESP32 timings. The full host run passed 180 regressions, and the final
changed acoustic subset passed 62. Browser checks passed 13 tests; all 39 header
boundaries and the concurrency/boot contracts passed. The backend's 88 tests
passed with three environment skips; the skipped local TLS test was then rerun
successfully with the existing OpenSSL installation. The two case-sensitive
filesystem tests remain Windows skips. Host object sizes were 12,208 bytes for
`NoteEstimator` and 121,504 bytes for `AcousticDSP` (which includes the estimator);
the regression caps are 16 KiB and 128 KiB respectively.

Existing authorized private recordings of one C-major reference loop were replayed
without opening a microphone. With the same >−50 dBFS gate:

| Recording | Correct displayed frames | Uncertain | Wrong | Correct fraction |
| --- | ---: | ---: | ---: | ---: |
| Windows default input | 34 / 52 | 18 | 0 | 65.4% |
| HyperX | 38 / 47 | 9 | 0 | 80.9% |

Neither clip produced a wrong displayed chord across all 119 frames, including
quieter intervals. This is **two captures of one reference, not a diverse
real-audio accuracy benchmark**. The new architecture is implemented; general
microphone accuracy, inversion stability, DSP latency and RX continuity still
require physical-board validation after the user compiles. No 100% recognition
claim is made. The historical intermediate detector's results are retained in
the v2.5.0 report and should not be confused with this pipeline.

To replay your own labeled single-chord recording:

```powershell
python tests/firmware/run_tests.py --only acoustic-dsp
python tests/firmware/replay_chords.py sample.pcm --sample-rate 48000 --expected Cmaj
```

Supply signed 16-bit little-endian mono PCM at its actual rate. The opt-in script
reports correct, uncertain and wrong separately, opens no microphone and sends
no audio over the network. Its configurable acceptance gate is not a probability
calibration or a substitute for varied recordings of guitars, keyboards, bass,
voicings and real transitions. Do not label a progression as one sustained chord.
