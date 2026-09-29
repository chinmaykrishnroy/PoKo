#include <cassert>
#include <cstring>
#include "ButtonInput.h"
#undef assert
#define assert(condition) do { if (!(condition)) { puts("FAIL: " #condition); return 1; } } while (0)
uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
int clicks = 0, doubles = 0, longs = 0, ramps = 0;
bool asleep = false;
int wakes = 0, combos = 0, comboDoubles = 0, reboot = 0, drivers = 0, emergency = 0;
void advance(ButtonInput& input, int ms) {
    for (int i = 0; i < ms; i += 5) { testMillis += 5; input.update(); }
}
int main(int argc, char** argv) {
    std::fill_n(testPins, 64, HIGH);
    ButtonInput input;
    if (!strcmp(argv[1], "deferred-init")) {
        assert(testPinSetups == 0 && "GPIO must not initialize before setup");
        input.begin();
        assert(testPinSetups == 3);
        return 0;
    }
    input.begin();
    input.onDown([] { ++clicks; }); input.onUp([] { ++clicks; });
    input.onDownDouble([] { ++doubles; }); input.onUpDouble([] { ++doubles; });
    input.onLongUp([] { ++longs; });
    input.onUpHolding([] { ++ramps; });
    input.onPwrClick([] { ++clicks; });
    input.onPwrLong([] { ++longs; });
    input.onBothClick([] { ++combos; });
    input.onBothDouble([] { ++comboDoubles; });
    input.onBothLong([] { ++reboot; });
    input.onBothVLong([] { ++drivers; });
    input.onBothUltra([] { ++emergency; });
    input.onPress([]() -> bool { if (!asleep) return false; asleep = false; ++wakes; return true; });
    int pin = !strcmp(argv[1], "double-left") ? POKO_PIN_BTN_DOWN : POKO_PIN_BTN_UP;
    if (!strncmp(argv[1], "double-", 7)) {
        testPins[pin] = LOW; advance(input, 70);
        testPins[pin] = HIGH; advance(input, 250);
        testPins[pin] = LOW; advance(input, 70);
        testPins[pin] = HIGH; advance(input, 500);
        assert(doubles == 1 && clicks == 0 && "double click must exit, not scroll twice");
    } else if (!strcmp(argv[1], "info-hold")) {
        testPins[POKO_PIN_BTN_UP] = LOW; advance(input, 1100);
        testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 600);
        assert(longs == 1 && ramps == 0 && clicks == 0 && "Info hold must refresh once without volume or scrolling");
    } else if (!strcmp(argv[1], "power-click")) {
        testPins[POKO_PIN_BTN_PWR] = LOW; advance(input, 70);
        testPins[POKO_PIN_BTN_PWR] = HIGH; advance(input, 60);
        assert(clicks == 1 && "power click must not wait for unused double click");
    } else if (!strncmp(argv[1], "wake-", 5)) {
        pin = !strcmp(argv[1], "wake-left") ? POKO_PIN_BTN_DOWN :
              !strcmp(argv[1], "wake-right") ? POKO_PIN_BTN_UP : POKO_PIN_BTN_PWR;
        asleep = true;
        testPins[pin] = LOW; advance(input, 40);
        assert(wakes == 1 && "wake must occur on debounced press, before release");
        advance(input, 2600); // a held wake press must not shut down or ramp
        testPins[pin] = HIGH; advance(input, 120);
        testPins[pin] = LOW; advance(input, 80); // second tap belongs to wake gesture
        testPins[pin] = HIGH; advance(input, 700);
        assert(clicks == 0 && doubles == 0 && longs == 0 && ramps == 0);
        testPins[pin] = LOW; advance(input, 80);
        testPins[pin] = HIGH; advance(input, 600);
        assert(clicks == 1 && "next intentional click after wake must work");
    } else if (!strcmp(argv[1], "music-hold")) {
        input.setHoldRepeatEnabled(true);
        testPins[POKO_PIN_BTN_UP] = LOW; advance(input, 1500);
        testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 600);
        assert(ramps >= 6 && longs == 0 && clicks == 0);
    } else if (!strcmp(argv[1], "single-left") || !strcmp(argv[1], "single-right")) {
        pin = !strcmp(argv[1], "single-left") ? POKO_PIN_BTN_DOWN : POKO_PIN_BTN_UP;
        testPins[pin] = LOW; advance(input, 80);
        testPins[pin] = HIGH; advance(input, 360);
        assert(clicks == 1 && doubles == 0 && "single click should dispatch within 360ms of release");
    } else if (!strcmp(argv[1], "busy-main-loop")) {
        // The worker samples a whole gesture while the app is blocked on I/O.
        for (int elapsed = 0; elapsed < 1200; elapsed += 5) {
            testPins[POKO_PIN_BTN_UP] = (elapsed < 80 || (elapsed >= 330 && elapsed < 410)) ? LOW : HIGH;
            testMillis += 5; input.sample();
        }
        assert(clicks == 0 && doubles == 0 && "worker must never call app code");
        input.update();
        assert(doubles == 1 && clicks == 0);
    } else if (!strcmp(argv[1], "bounce")) {
        for (int i = 0; i < 4; ++i) {
            testPins[POKO_PIN_BTN_UP] = LOW; advance(input, 10);
            testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 10);
        }
        advance(input, 600);
        assert(clicks == 0 && doubles == 0);
    } else if (!strcmp(argv[1], "overflow")) {
        input.setHoldRepeatEnabled(true);
        testPins[POKO_PIN_BTN_UP] = LOW;
        for (int elapsed = 0; elapsed < 12000; elapsed += 5) { testMillis += 5; input.sample(); }
        input.update();
        assert(ramps == 0 && "overflow must discard stale actions");
        testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 700);
        testPins[POKO_PIN_BTN_UP] = LOW; advance(input, 80);
        testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 600);
        assert(clicks == 1);
    } else if (!strncmp(argv[1], "combo-", 6)) {
        int held = !strcmp(argv[1], "combo-reboot") ? 2600 : !strcmp(argv[1], "combo-drivers") ? 5100 :
                   !strcmp(argv[1], "combo-emergency") ? 10100 : 100;
        testPins[POKO_PIN_BTN_UP] = testPins[POKO_PIN_BTN_DOWN] = LOW; advance(input, held);
        assert(reboot == 0 && drivers == 0 && emergency == 0);
        testPins[POKO_PIN_BTN_DOWN] = HIGH; advance(input, 60);
        testPins[POKO_PIN_BTN_UP] = HIGH; advance(input, 100);
        if (!strcmp(argv[1], "combo-double")) {
            testPins[POKO_PIN_BTN_UP] = testPins[POKO_PIN_BTN_DOWN] = LOW; advance(input, 100);
            testPins[POKO_PIN_BTN_UP] = testPins[POKO_PIN_BTN_DOWN] = HIGH;
        }
        advance(input, 700);
        assert(clicks == 0 && doubles == 0 && longs == 0 && ramps == 0);
        assert(reboot + drivers + emergency + combos + comboDoubles == 1);
        assert(held == 2600 ? reboot == 1 : held == 5100 ? drivers == 1 : held == 10100 ? emergency == 1 :
               !strcmp(argv[1], "combo-double") ? comboDoubles == 1 : combos == 1);
    } else { puts("Unknown test"); return 2;
    }
    puts("PASS");
}

