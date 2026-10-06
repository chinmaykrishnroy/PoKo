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
    } else if (test == "display-independent-timeouts") {
        CHECK(displayIdleAction(29999, 0, 30, false) == DISPLAY_IDLE_NONE);
        CHECK(displayIdleAction(30000, 0, 30, false) == DISPLAY_IDLE_SLEEP);
        CHECK(displayIdleAction(30000, 60, 30, false) == DISPLAY_IDLE_SLEEP);
        CHECK(displayIdleAction(15000, 15, 30, false) == DISPLAY_IDLE_DIM);
        CHECK(displayIdleAction(30000, 15, 30, true) == DISPLAY_IDLE_DIM);
    } else if (test == "idle-power-stages") {
        CHECK(!idlePowerStageDue(0, 999999, true, false, false));
        CHECK(!idlePowerStageDue(120, 120000, false, false, false));
        CHECK(!idlePowerStageDue(120, 120000, true, true, false));
        CHECK(!idlePowerStageDue(120, 120000, true, false, true));
        CHECK(!idlePowerStageDue(120, 119999, true, false, false));
        CHECK(idlePowerStageDue(120, 120000, true, false, false));
    } else if (test == "continuous-media-blocks-all-stages") {
        const uint32_t lastUser = 1;
        for (int source = 0; source < 3; ++source) { // SSync, Music, Video
            for (uint32_t now = 10000; now <= 86400000; now += 3600000) {
                uint32_t lastMedia = now;
                CHECK(protectedMediaHeld(true, now, lastMedia));
                CHECK(effectivePowerIdleMs(now, lastUser, lastMedia) == 0);
                CHECK(!idlePowerStageDue(2, 0, true, true, false));
                CHECK(!idlePowerStageDue(3, 0, true, true, false));
                CHECK(!idlePowerStageDue(4, 0, true, true, false));
                CHECK(!shouldAutoPowerOff(5, 5, 80, true, false,
                                          true, false, now));
            }
        }
    } else if (test == "media-stop-starts-idle-progression") {
        const uint32_t stoppedAt = 100000;
        CHECK(protectedMediaHeld(false, stoppedAt + 4999, stoppedAt));
        CHECK(!protectedMediaHeld(false, stoppedAt + 5000, stoppedAt));
        CHECK(effectivePowerIdleMs(stoppedAt + 1000, 1, stoppedAt) == 1000);
        CHECK(effectivePowerIdleMs(stoppedAt + 120000, 1, stoppedAt) == 120000);
    } else if (test == "media-low-battery-override") {
        CHECK(shouldAutoPowerOff(5, 5, 5, true, false,
                                 true, false, 5000));
    } else if (test == "light-sleep-next-deadline") {
        CHECK(nextLightSleepWakeMs(100000, 600, 900) == 20000);
        CHECK(nextLightSleepWakeMs(590000, 600, 900) == 10000);
        CHECK(nextLightSleepWakeMs(599999, 600, 900) == 1);
        CHECK(nextLightSleepWakeMs(1000, 0, 0) == 20000);
    } else if (test == "network-wake-defers-actions") {
        CHECK(networkActionShouldWait(true, false, false, true));
        CHECK(networkActionShouldWait(true, false, true, false));
        CHECK(!networkActionShouldWait(true, true, true, false));
        CHECK(!networkActionShouldWait(false, false, true, true));
    } else {
        return 2;
    }
    return 0;
}
