#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <esp_wifi.h>
#include <functional>
#include <atomic>
#include "PokoPins.h"
#include "PokoDrivers.h"
#include "BatteryManager.h"
#include "AudioManager.h"
#include "PixelEngine.h"
#include "PowerPolicy.h"

// ─────────────────────────────────────────────────────────────
//  PowerManager — Resource-aware embedded power management
//  Hardware: Waveshare ESP32-S3-LCD-0.85
//  Controls:
//    - Logical capability/lock bitmask (POWER_LOCK_*)
//    - Layered GC9107 display power (Active, Dimmed, Sleep/Off)
//    - Smooth non-blocking backlight fading
//    - Pop-safe speaker amplifier PA standby after audio idle
//    - Dynamic Wi-Fi modem sleep vs. zero-jitter SSync protection
//    - Battery-aware inactivity auto-shutdown
//    - Graceful power-off (BAT_EN = LOW / Deep Sleep fallback)
// ─────────────────────────────────────────────────────────────

enum PowerLock : uint32_t {
    POWER_LOCK_NONE                 = 0,
    POWER_LOCK_DISPLAY              = 1 << 0, // Keep display active (Video playback, slideshow, manual override)
    POWER_LOCK_AUDIO                = 1 << 1, // Audio playback active (Music, SSync, Video)
    POWER_LOCK_REALTIME_NET         = 1 << 2, // Low-latency Wi-Fi required (SSync sync, video streaming) -> disables Wi-Fi modem sleep
    POWER_LOCK_PREVENT_LIGHT_SLEEP  = 1 << 3, // Prevent automatic light sleep
    POWER_LOCK_PREVENT_DEEP_SLEEP   = 1 << 4, // Prevent auto power-off or deep sleep
    POWER_LOCK_LEDS                 = 1 << 5, // Keep LEDs enabled during display sleep
    POWER_LOCK_OTA                  = 1 << 6  // OTA flashing in progress
};

enum PowerLockOwner : uint8_t {
    LOCK_OWNER_SYSTEM  = 0,
    LOCK_OWNER_AUDIO   = 1,
    LOCK_OWNER_VIDEO   = 2,
    LOCK_OWNER_GALLERY = 3,
    LOCK_OWNER_OTA     = 4,
    LOCK_OWNER_COUNT   = 5
};

enum DisplayPowerState : uint8_t {
    DISPLAY_POWER_ACTIVE = 0, // User-configured full brightness
    DISPLAY_POWER_DIMMED,     // Reduced brightness (15-20%)
    DISPLAY_POWER_OFF,        // Backlight 0, GC9107 sleep
    DISPLAY_POWER_SLEEP       // Alias
};

enum ActivitySource : uint8_t {
    ACTIVITY_USER = 0,
    ACTIVITY_BUTTON,
    ACTIVITY_WEB_COMMAND,
    ACTIVITY_MEDIA
};

enum ProtectedActivityKind : uint8_t {
    PROTECTED_NONE = 0,
    PROTECTED_SSYNC,
    PROTECTED_MUSIC,
    PROTECTED_VIDEO,
    PROTECTED_TORCH,
    PROTECTED_MIC
};

class PowerManager {
private:
    BatteryManager*       _battery;
    AudioManager*         _audio;
    Preferences*          _prefs;
    std::function<void()> _wakeCb = nullptr;
    std::function<void()> _wifiHibernateCb = nullptr;
    std::function<void()> _networkWakeCb = nullptr;
    std::function<void()> _powerDownCb = nullptr;

    std::atomic<uint32_t> _locks{POWER_LOCK_NONE};
    uint32_t              _ownerLocks[LOCK_OWNER_COUNT] = {0};
    DisplayPowerState     _displayState        = DISPLAY_POWER_ACTIVE;

    uint32_t              _lastActivityMs      = 0;
    uint32_t              _lastProtectedActivityMs = 0;
    uint32_t              _lastAudioActiveMs   = 0;
    uint32_t              _lastDimStepMs       = 0;
    uint32_t              _lastPowerTraceMs    = 0;

    // Timeout settings (in seconds)
    uint32_t              _dimTimeoutSec       = 15;
    uint32_t              _sleepTimeoutSec     = 30;
    uint32_t              _autoOffSec          = 900; // 15 min default
    uint8_t               _autoOffBatteryPct   = 5;
    bool                  _protectedActivity   = false;
    ProtectedActivityKind _protectedKind       = PROTECTED_NONE;
    bool                  _autoOffTiming       = false;
    bool                  _ambientClockEnabled = false;

    // Backlight ramp
    int                   _userBrightnessPercent = 80;
    uint8_t               _targetDuty          = 204; // 80% default
    uint8_t               _currentDuty         = 204;
    bool                  _wifiSleepEnabled    = false;
    bool                  _paStandbyDone       = false;
    bool                  _audioHardwareOff    = true;
    bool                  _usbPerfMax          = true;
    bool                  _wifiHibernated      = false;
    uint32_t              _wifiHibernateSec    = 120;
    uint32_t              _lightSleepSec       = 120;
    uint32_t              _deepSleepSec        = 600;
    uint8_t               _idleCpuMhz          = 80;
    uint8_t               _loopYieldMs         = 2;
    SemaphoreHandle_t     _lockMutex           = nullptr;

    void _recalcLocks();
    void _enterLightSleep(uint32_t idleMs);
    void _enterDeepIdle(uint32_t idleMs);

public:
    void setWakeCallback(std::function<void()> cb);
    void setNetworkCallbacks(std::function<void()> hibernateCb,
                             std::function<void()> wakeCb);
    uint8_t percentToDuty(int pct) const;

    int getUserBrightnessPercent() const;

    bool isBatteryPresent() const;

    bool hasSustainedPowerForMaxPerformance() const;

    bool isUsbPerfMax() const;

    void setUsbPerfMax(bool en);

    void setWifiSleepAllowed(bool en, bool persist = true);
    bool isWifiSleepAllowed() const;

    void setWifiHibernated(bool hibernated);
    bool isWifiHibernated() const;

    void setWifiHibernateTimeout(uint32_t sec, bool persist = true);
    uint32_t getWifiHibernateTimeout() const;

    void setLightSleepTimeout(uint32_t sec, bool persist = true);
    uint32_t getLightSleepTimeout() const;

    void setDeepSleepTimeout(uint32_t sec, bool persist = true);
    uint32_t getDeepSleepTimeout() const;

    void setIdleCpuMhz(uint8_t mhz, bool persist = true);
    uint8_t getIdleCpuMhz() const;

    void setLoopYieldMs(uint8_t ms, bool persist = true);
    uint8_t getLoopYieldMs() const;
    uint8_t recommendedLoopYieldMs() const;

    bool isMaxPerfActive() const;

    bool isAmbientClockEnabled() const;

    PowerManager(BatteryManager* bat, AudioManager* audio, Preferences* prf);

    void begin();

    // ── Lock Management ──────────────────────────────────────────
    void acquireLock(uint32_t mask, PowerLockOwner owner = LOCK_OWNER_SYSTEM);

    void releaseLock(uint32_t mask, PowerLockOwner owner = LOCK_OWNER_SYSTEM);

    bool hasLock(uint32_t mask) const;

    uint32_t getLocks() const;

    // ── Activity Tracking ────────────────────────────────────────
    bool notifyUserActivity(ActivitySource src = ACTIVITY_USER);

    // ── Display Power State Machine ──────────────────────────────
    DisplayPowerState getDisplayState() const;

    void wakeDisplay();

    void dimDisplay();

    void sleepDisplay();

    void toggleScreen();

    // ── Graceful Shutdown Sequence ───────────────────────────────
    void powerOff(bool restart = false);
    void setPowerDownCallback(std::function<void()> cb);

    // ── Main Update Loop ─────────────────────────────────────────
    void update();

    // ── Setters & Getters for Settings / API ─────────────────────
    void setBrightnessPercent(int pct, bool persist = true);

    void setDimTimeout(uint32_t sec, bool persist = true);
    uint32_t getDimTimeout() const;

    void setSleepTimeout(uint32_t sec, bool persist = true);
    uint32_t getSleepTimeout() const;

    void setAutoOffTimeout(uint32_t sec, bool persist = true);
    uint32_t getAutoOffTimeout() const;

    void setAutoOffBatteryPercent(uint8_t percent, bool persist = true);
    uint8_t getAutoOffBatteryPercent() const;

    void setProtectedActivity(bool active,
                              ProtectedActivityKind kind = PROTECTED_NONE);
    bool isProtectedActivity() const;

    void setAmbientClock(bool en);
    bool isAmbientClock() const;

    uint32_t getIdleSeconds() const;

    // ── Telemetry JSON Builder for /api/power ───────────────────
    String getTelemetryJson() const;
};

// Called before normal hardware initialization. This is deep sleep plus reboot;
// RTC-retained state lets a timer wake complete the pending inactivity cutoff.
bool completePendingIdlePowerOffWake();

