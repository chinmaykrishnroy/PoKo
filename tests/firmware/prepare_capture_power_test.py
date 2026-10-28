"""Exercise the current PowerManager audio coordination, not a copied policy."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=(root/"PowerManager.cpp").read_text(encoding="utf-8")
audio=source[source.index("        // 2. Audio Subsystem"):source.index("        // 3. Dynamic Wi-Fi")]
start=source.index("uint8_t PowerManager::recommendedLoopYieldMs() const {")
end=source.index("bool PowerManager::isMaxPerfActive() const", start)
yield_method=source[start:end].replace("PowerManager::", "")
(root/"build/capture-power-test.cpp").write_text(
    '#include "capture_power_fixture.h"\nclass PowerHarness {\npublic:\n' +
    'AudioManager* _audio=&audio; bool _audioHardwareOff=true,_paStandbyDone=true,_protectedActivity=true;\n' +
    'uint32_t _lastAudioActiveMs=0;uint8_t _loopYieldMs=10;\n' +
    'bool hasLock(int bit) const {return (locks & bit)!=0;}\n' +
    'void update(uint32_t now) {\n' + audio + '}\n' + yield_method + '};\n' +
    '#include "capture_power_main.cpp"\n', encoding="utf-8")
