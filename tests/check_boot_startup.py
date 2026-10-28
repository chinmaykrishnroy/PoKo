"""Keep connectivity startup independent of the boot animation."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "PoKo.ino").read_text()
setup = source[source.index("void setup() {"):source.index("void loop() {")]
loop = source[source.index("void loop() {"):]
splash = (root / "BootSplash.cpp").read_text()
assert setup.index("WiFi.begin(") < setup.index("snapService = new"), "Radio startup must precede service/app allocation"
assert "bootSplash.finish()" not in setup, "Setup must not wait for splash completion"
assert loop.index("server.handleClient()") < loop.index("if (bootSplashVisible) {"), "HTTP must run before splash/UI updates"
assert loop.index("ArduinoOTA.handle()") < loop.index("if (bootSplashVisible) {"), "OTA must run before splash/UI updates"
assert "delay(" not in splash and "while (" not in splash, "Splash code must never wait"
print("PASS: connectivity starts before apps and is serviced independently of splash frames")
