#pragma once
#include <stdint.h>

bool shouldAutoPowerOff(uint32_t delaySec,
                        uint8_t thresholdPercent,
                        int batteryPercent,
                        bool batteryPresent,
                        bool charging,
                        bool mediaAppActive,
                        bool shutdownLocked,
                        uint32_t belowThresholdMs);
