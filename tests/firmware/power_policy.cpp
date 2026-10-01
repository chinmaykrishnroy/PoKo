#include <cstdlib>
#include <iostream>
#include "PowerPolicy.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed: " #x << '\n'; return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    if (test == "media-inhibits") {
        CHECK(!shouldAutoPowerOff(900, 5, 4, true, false, true, false, 900000));
    } else if (test == "threshold-delay") {
        CHECK(!shouldAutoPowerOff(900, 5, 4, true, false, false, false, 899999));
        CHECK(shouldAutoPowerOff(900, 5, 4, true, false, false, false, 900000));
    } else if (test == "above-threshold") {
        CHECK(!shouldAutoPowerOff(900, 5, 6, true, false, false, false, 9999999));
    } else if (test == "charging-inhibits") {
        CHECK(!shouldAutoPowerOff(900, 5, 2, true, true, false, false, 9999999));
    } else if (test == "disabled") {
        CHECK(!shouldAutoPowerOff(900, 0, 2, true, false, false, false, 9999999));
        CHECK(!shouldAutoPowerOff(0, 5, 2, true, false, false, false, 9999999));
    } else {
        return 2;
    }
    return 0;
}
