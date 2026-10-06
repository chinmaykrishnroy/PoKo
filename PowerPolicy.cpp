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

DisplayIdleAction displayIdleAction(uint32_t idleMs,
                                    uint32_t dimTimeoutSec,
                                    uint32_t sleepTimeoutSec,
                                    bool ambientClock) {
    if (!ambientClock && sleepTimeoutSec > 0 &&
        idleMs >= sleepTimeoutSec * 1000UL) {
        return DISPLAY_IDLE_SLEEP;
    }
    if (dimTimeoutSec > 0 && idleMs >= dimTimeoutSec * 1000UL) {
        return DISPLAY_IDLE_DIM;
    }
    return DISPLAY_IDLE_NONE;
}

bool idlePowerStageDue(uint32_t timeoutSec, uint32_t idleMs,
                       bool displayOff, bool protectedActivity,
                       bool blockingLock) {
    return timeoutSec > 0 && displayOff && !protectedActivity &&
           !blockingLock && idleMs >= timeoutSec * 1000UL;
}

uint32_t effectivePowerIdleMs(uint32_t nowMs,
                              uint32_t lastUserActivityMs,
                              uint32_t lastProtectedMediaMs) {
    uint32_t userIdle = nowMs - lastUserActivityMs;
    uint32_t mediaIdle = nowMs - lastProtectedMediaMs;
    return userIdle < mediaIdle ? userIdle : mediaIdle;
}

bool protectedMediaHeld(bool currentlyActive,
                        uint32_t nowMs,
                        uint32_t lastProtectedMediaMs,
                        uint32_t releaseGraceMs) {
    return currentlyActive ||
           (lastProtectedMediaMs != 0 &&
            (nowMs - lastProtectedMediaMs) < releaseGraceMs);
}

uint32_t nextLightSleepWakeMs(uint32_t idleMs,
                              uint32_t deepSleepSec,
                              uint32_t autoOffSec,
                              uint32_t capMs) {
    uint32_t wakeMs = capMs > 0 ? capMs : 20000;
    const uint32_t deadlines[] = {deepSleepSec, autoOffSec};
    for (uint32_t seconds : deadlines) {
        if (seconds == 0) continue;
        uint64_t deadlineMs = (uint64_t)seconds * 1000ULL;
        if (deadlineMs <= idleMs) return 1;
        uint64_t remaining = deadlineMs - idleMs;
        if (remaining < wakeMs) wakeMs = (uint32_t)remaining;
    }
    return wakeMs == 0 ? 1 : wakeMs;
}

bool networkActionShouldWait(bool networkRequired,
                             bool connected,
                             bool wakeInProgress,
                             bool hibernated) {
    return networkRequired && !connected && (wakeInProgress || hibernated);
}
