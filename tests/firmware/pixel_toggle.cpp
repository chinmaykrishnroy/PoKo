#include <cassert>
#include <cstring>
#include "PixelToggle.h"

enum Mode { OFF, SOLID, RAINBOW };

struct PreferencesMock {};

struct EngineMock {
    Mode mode = OFF;
    int r = 11, g = 22, b = 33;
    int saves = 0;
    Mode getMode() const { return mode; }
    void setMode(Mode value) { mode = value; }
    void saveToPreferences(PreferencesMock&) { ++saves; }
};

int main(int argc, char** argv) {
    EngineMock engine;
    PreferencesMock prefs;
    if (!strcmp(argv[1], "preserves-color")) {
        togglePixelSolidOff(engine, prefs, OFF, SOLID);
        assert(engine.r == 11 && engine.g == 22 && engine.b == 33);
    } else if (!strcmp(argv[1], "forces-solid")) {
        engine.mode = RAINBOW;
        togglePixelSolidOff(engine, prefs, OFF, SOLID);
        assert(engine.mode == OFF);
        togglePixelSolidOff(engine, prefs, OFF, SOLID);
        assert(engine.mode == SOLID && engine.saves == 2);
    } else if (!strcmp(argv[1], "turns-off")) {
        engine.mode = SOLID;
        togglePixelSolidOff(engine, prefs, OFF, SOLID);
        assert(engine.mode == OFF && engine.saves == 1);
    }
}
