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

enum DisplayIdleAction : uint8_t {
    DISPLAY_IDLE_NONE = 0,
    DISPLAY_IDLE_DIM,
    DISPLAY_IDLE_SLEEP
};

DisplayIdleAction displayIdleAction(uint32_t idleMs,
                                    uint32_t dimTimeoutSec,
                                    uint32_t sleepTimeoutSec,
                                    bool ambientClock);

bool idlePowerStageDue(uint32_t timeoutSec, uint32_t idleMs,
                       bool displayOff, bool protectedActivity,
                       bool blockingLock);

// User interaction and media activity are separate clocks. Low-power stages
// start from whichever occurred most recently without pretending media is a
// user gesture.
uint32_t effectivePowerIdleMs(uint32_t nowMs,
                              uint32_t lastUserActivityMs,
                              uint32_t lastProtectedMediaMs);

// Keep power locks across short renderer-state gaps. This prevents one stale
// or transient isPlaying() sample from exposing already-expired idle timers.
bool protectedMediaHeld(bool currentlyActive,
                        uint32_t nowMs,
                        uint32_t lastProtectedMediaMs,
                        uint32_t releaseGraceMs = 5000);

// Light sleep wakes for the next real power deadline, with a bounded health
// check so button and state handling are never deferred indefinitely.
uint32_t nextLightSleepWakeMs(uint32_t idleMs,
                              uint32_t deepSleepSec,
                              uint32_t autoOffSec,
                              uint32_t capMs = 20000);

bool networkActionShouldWait(bool networkRequired,
                             bool connected,
                             bool wakeInProgress,
                             bool hibernated);
