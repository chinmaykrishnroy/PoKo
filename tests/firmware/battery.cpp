#include <iostream>
#include <string>
#include "Arduino.h"
#include "BatteryManager.h"
#include "PokoPins.h"

uint32_t testMillis = 0;
int testPins[64] = {};
int testPinSetups = 0;
uint32_t testAdcMilliVolts = 1300;

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed: " #x << '\n'; return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    testPins[POKO_PIN_CHARGING] = HIGH;
    BatteryManager battery;
    battery.begin();
    CHECK(!battery.isCharging());

    if (test == "percentage-slew") {
        int initial = battery.getPercentage();
        testPins[POKO_PIN_CHARGING] = LOW;
        testMillis = 50;
        battery.update();
        testMillis = 100;
        battery.update();
        CHECK(battery.isCharging());

        testAdcMilliVolts = 1400;
        battery.readNow();
        CHECK(battery.getPercentage() == initial);
        testMillis = 30100;
        battery.readNow();
        CHECK(battery.getPercentage() >= initial);
        CHECK(battery.getPercentage() <= initial + 3);
        return 0;
    }
    if (test != "periodic-read-keeps-plug-event") return 2;

    testPins[POKO_PIN_CHARGING] = LOW;
    testMillis = 50;
    battery.update();
    battery.readNow();
    testMillis = 100;
    battery.update();

    CHECK(battery.isCharging());
    CHECK(battery.consumePluggedInEvent());
    CHECK(!battery.consumePluggedInEvent());
    return 0;
}
