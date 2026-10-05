#include "PowerPolicy.h"

bool protectedActivityCanInhibitAutoOff(uint8_t thresholdPercent,
                                        int batteryPercent,
                                        bool batteryPresent,
                                        bool charging,
                                        bool protectedActivity) {
    if (!protectedActivity) return false;

    // External power and devices without a measurable battery can safely keep
    // an active stream or torch running. A zero threshold disables the
    // low-battery override, so protected activity may always inhibit auto-off.
    if (!batteryPresent || charging || thresholdPercent == 0 || batteryPercent < 0) return true;
    return batteryPercent > thresholdPercent;
}

bool shouldAutoPowerOff(uint32_t delaySec,
                        uint8_t thresholdPercent,
                        int batteryPercent,
                        bool batteryPresent,
                        bool charging,
                        bool protectedActivity,
                        bool shutdownLocked,
                        uint32_t inactiveMs) {
    if (delaySec == 0 || shutdownLocked) return false;
    if (protectedActivityCanInhibitAutoOff(thresholdPercent, batteryPercent,
                                           batteryPresent, charging, protectedActivity)) {
        return false;
    }
    return inactiveMs >= delaySec * 1000UL;
}
