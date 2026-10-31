"""Check an opt-in, known single-chord recording through the production DSP.

Build the host executable with run_tests.py --only acoustic-dsp first. Audio
stays local; no microphone is opened. The input must be signed 16-bit LE mono
PCM, with its actual sample rate supplied explicitly. Do not use a progression
as ground truth for a single chord.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("pcm", type=Path)
    parser.add_argument("--sample-rate", type=int, required=True)
    parser.add_argument("--expected", required=True, choices=[
        root + quality for root in ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
        for quality in ("maj", "m", "sus2", "sus4", "7", "maj7", "m7", "aug", "dim", "dim7", "m7b5", "6", "m6", "add9", "madd9", "5")])
    parser.add_argument("--active-dbfs", type=float, default=-50)
    parser.add_argument("--min-correct", type=float, default=0.65)
    parser.add_argument("--max-wrong", type=int, default=0)
    parser.add_argument("--binary", type=Path, help="Alternate host replay executable for baseline comparisons")
    args = parser.parse_args()
    if not 8000 <= args.sample_rate <= 192000 or not 0 <= args.min_correct <= 1 or args.max_wrong < 0:
        parser.error("Invalid clock or acceptance threshold")
    if not args.pcm.is_file() or args.pcm.stat().st_size == 0 or args.pcm.stat().st_size % 2:
        parser.error("PCM must be a nonempty file of complete 16-bit samples")
    root = Path(__file__).resolve().parents[2]
    binary = args.binary or root / "build/host-tests" / ("acoustic-dsp.exe" if os.name == "nt" else "acoustic-dsp")
    result = subprocess.run([str(binary), "replay", str(args.pcm), str(args.sample_rate)],
                            check=True, capture_output=True, text=True)
    active, all_frames = Counter(), Counter()
    for line in result.stdout.splitlines():
        _, rms, _, displayed = line.split(",")
        all_frames[displayed] += 1
        if float(rms) > args.active_dbfs:
            active[displayed] += 1
    total = sum(active.values())
    correct = active[args.expected] / total if total else 0
    wrong = sum(n for chord, n in active.items() if chord not in (args.expected, "--"))
    print(json.dumps({"active_frames": dict(active), "all_frames": dict(all_frames),
                      "correct_fraction": round(correct, 4), "wrong_active_frames": wrong}, indent=2))
    return 0 if total and correct >= args.min_correct and wrong <= args.max_wrong else 1


if __name__ == "__main__":
    raise SystemExit(main())
