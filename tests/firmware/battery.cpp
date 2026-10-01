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
    if (argc != 2 || std::string(argv[1]) != "periodic-read-keeps-plug-event") return 2;
    testPins[POKO_PIN_CHARGING] = HIGH;
    BatteryManager battery;
    battery.begin();
    CHECK(!battery.isCharging());

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
