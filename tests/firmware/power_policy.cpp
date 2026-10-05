#include <cstdlib>
#include <iostream>
#include "PowerPolicy.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed: " #x << '\n'; return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    if (test == "active-above-threshold-inhibits") {
        CHECK(!shouldAutoPowerOff(900, 5, 6, true, false, true, false, 9999999));
        CHECK(!shouldAutoPowerOff(900, 5, 2, true, true, true, false, 9999999));
        CHECK(!shouldAutoPowerOff(900, 5, -1, false, false, true, false, 9999999));
    } else if (test == "inactive-delay") {
        CHECK(!shouldAutoPowerOff(900, 5, 80, true, false, false, false, 899999));
        CHECK(shouldAutoPowerOff(900, 5, 80, true, false, false, false, 900000));
    } else if (test == "low-battery-overrides-activity") {
        CHECK(!shouldAutoPowerOff(900, 5, 5, true, false, true, false, 899999));
        CHECK(shouldAutoPowerOff(900, 5, 5, true, false, true, false, 900000));
    } else if (test == "charging-idle-still-powers-off") {
        CHECK(shouldAutoPowerOff(900, 5, 80, true, true, false, false, 900000));
    } else if (test == "disabled") {
        CHECK(!shouldAutoPowerOff(0, 5, 2, true, false, false, false, 9999999));
        CHECK(!shouldAutoPowerOff(900, 0, 2, true, false, true, false, 9999999));
        CHECK(shouldAutoPowerOff(900, 0, 2, true, false, false, false, 900000));
        CHECK(!shouldAutoPowerOff(900, 5, 2, true, false, false, true, 9999999));
    } else {
        return 2;
    }
    return 0;
}
