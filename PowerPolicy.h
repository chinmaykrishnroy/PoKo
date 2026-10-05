#pragma once
#include <stdint.h>

bool shouldAutoPowerOff(uint32_t delaySec,
                        uint8_t thresholdPercent,
                        int batteryPercent,
                        bool batteryPresent,
                        bool charging,
                        bool protectedActivity,
                        bool shutdownLocked,
                        uint32_t inactiveMs);

bool protectedActivityCanInhibitAutoOff(uint8_t thresholdPercent,
                                        int batteryPercent,
                                        bool batteryPresent,
                                        bool charging,
                                        bool protectedActivity);
