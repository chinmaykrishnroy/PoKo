#include "PowerPolicy.h"

bool shouldAutoPowerOff(uint32_t delaySec,
                        uint8_t thresholdPercent,
                        int batteryPercent,
                        bool batteryPresent,
                        bool charging,
                        bool mediaAppActive,
                        bool shutdownLocked,
                        uint32_t belowThresholdMs) {
    if (delaySec == 0 || thresholdPercent == 0) return false;
    if (!batteryPresent || charging || mediaAppActive || shutdownLocked) return false;
    if (batteryPercent < 0 || batteryPercent > thresholdPercent) return false;
    return belowThresholdMs >= delaySec * 1000UL;
}
