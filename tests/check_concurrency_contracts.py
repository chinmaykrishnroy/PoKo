"""Reject volatile as a substitute for inter-task synchronization."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"es8311.h", "es8311_reg.h"}


def main() -> int:
    errors: list[str] = []
    pattern = re.compile(r"\bvolatile\b")
    for path in sorted(ROOT.glob("*.h")) + sorted(ROOT.glob("*.cpp")):
        if path.name in EXCLUDED:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if pattern.search(line):
                errors.append(f"{path.name}:{line_number}: volatile shared state is forbidden; use atomics, queues, or locks")
    if errors:
        print("\n".join(errors))
        return 1
    print("PASS: firmware task-sharing contracts do not rely on volatile")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
