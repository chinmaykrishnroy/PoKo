"""Run the actual shutdown method against a USB-powered GPIO/clock model."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = (root / "PowerManager.cpp").read_text()
start = source.index("void PowerManager::powerOff(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
setup = (root / "PoKo.ino").read_text()
prefix_start = setup.index("{", setup.index("void setup()")) + 1
prefix_end = setup.index("Serial.begin(115200);", prefix_start)
(root / "build/power-wake-method.inc").write_text(source[start:end] +
    "\nvoid bootUntilSerial() {" + setup[prefix_start:prefix_end] + "}\n")
