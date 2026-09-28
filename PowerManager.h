#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <functional>
#include "PokoPins.h"
#include "PokoDrivers.h"
#include "BatteryManager.h"
#include "AudioManager.h"
#include "PixelEngine.h"

// ─────────────────────────────────────────────────────────────
//  PowerManager — Resource-aware embedded power management
//  Hardware: Waveshare ESP32-S3-LCD-0.85
//  Controls:
//    - Logical capability/lock bitmask (POWER_LOCK_*)
//    - Layered GC9107 display power (Active, Dimmed, Sleep/Off)
//    - Smooth non-blocking backlight fading
//    - Pop-safe speaker amplifier PA standby after audio idle
//    - Dynamic Wi-Fi modem sleep vs. zero-jitter SSync protection
//    - Battery monitoring and low-voltage auto-shutdown
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

class PowerManager {
private:
    BatteryManager*       _battery;
    AudioManager*         _audio;
    Preferences*          _prefs;
    std::function<void()> _wakeCb = nullptr;

    uint32_t              _locks               = POWER_LOCK_NONE;
    DisplayPowerState     _displayState        = DISPLAY_POWER_ACTIVE;

    uint32_t              _lastActivityMs      = 0;
    uint32_t              _lastAudioActiveMs   = 0;
    uint32_t              _lastDimStepMs       = 0;

    // Timeout settings (in seconds)
    uint32_t              _dimTimeoutSec       = 15;
    uint32_t              _sleepTimeoutSec     = 30;
    uint32_t              _autoOffSec          = 900; // 15 min default
    bool                  _ambientClockEnabled = false;

    // Backlight ramp
    uint8_t               _targetDuty          = 204; // 80% default
    uint8_t               _currentDuty         = 204;
    bool                  _wifiSleepEnabled    = false;
    bool                  _paStandbyDone       = false;
    bool                  _usbPerfMax          = true;
    SemaphoreHandle_t     _lockMutex           = nullptr;

public:
    void setWakeCallback(std::function<void()> cb) { _wakeCb = cb; }
    uint8_t percentToDuty(int pct) const {
        pct = constrain(pct, 0, 100);
        return (uint8_t)((pct * 255) / 100);
    }

    int getUserBrightnessPercent() const {
        if (_prefs) {
            return _prefs->getInt("brightness", 80);
        }
        return 80;
    }

    bool isBatteryPresent() const {
        return _battery && _battery->isPresent();
    }

    bool isUsbPowered() const {
        return !isBatteryPresent() || (_battery && (_battery->isCharging() || _battery->isFull()));
    }

    bool isUsbPerfMax() const {
        return _usbPerfMax;
    }

    void setUsbPerfMax(bool en) {
        _usbPerfMax = en;
        if (_prefs) _prefs->putBool("usb_perf", en);
        if (_usbPerfMax && isUsbPowered()) {
            WiFi.setSleep(false);
            _wifiSleepEnabled = false;
        }
    }

    bool isMaxPerfActive() const {
        return !isBatteryPresent() || (isUsbPowered() && _usbPerfMax);
    }

    bool isAmbientClockEnabled() const {
        return _ambientClockEnabled;
    }

    PowerManager(BatteryManager* bat, AudioManager* audio, Preferences* prf)
        : _battery(bat), _audio(audio), _prefs(prf) {
        _lockMutex = xSemaphoreCreateMutex();
    }

    void begin() {
        _lastActivityMs = millis();
        _lastAudioActiveMs = millis();

        if (_prefs) {
            _dimTimeoutSec = _prefs->getUInt("dim_timeout", 15);
            _sleepTimeoutSec = _prefs->getUInt("sleep_timeout", 30);
            _autoOffSec = _prefs->getUInt("auto_off", 900);
            _ambientClockEnabled = _prefs->getBool("ambient_clock", false);
            _usbPerfMax = _prefs->getBool("usb_perf", true);
        }

        int userBr = getUserBrightnessPercent();
        _targetDuty = percentToDuty(userBr);
        _currentDuty = _targetDuty;
        setBacklight(_currentDuty);

        Serial.printf("[power] PowerManager initialized (dim=%us, sleep=%us, auto_off=%us, usb_perf=%s)\n",
                      _dimTimeoutSec, _sleepTimeoutSec, _autoOffSec, _usbPerfMax ? "MaxPerf" : "Managed");
    }

    // ── Lock Management ──────────────────────────────────────────
    void acquireLock(uint32_t mask) {
        if (_lockMutex) xSemaphoreTake(_lockMutex, portMAX_DELAY);
        _locks |= mask;
        if (_lockMutex) xSemaphoreGive(_lockMutex);
    }

    void releaseLock(uint32_t mask) {
        if (_lockMutex) xSemaphoreTake(_lockMutex, portMAX_DELAY);
        _locks &= ~mask;
        if (_lockMutex) xSemaphoreGive(_lockMutex);
    }

    bool hasLock(uint32_t mask) const {
        return (_locks & mask) != 0;
    }

    uint32_t getLocks() const {
        return _locks;
    }

    // ── Activity Tracking ────────────────────────────────────────
    bool notifyUserActivity(ActivitySource src = ACTIVITY_USER) {
        _lastActivityMs = millis();

        if (_displayState != DISPLAY_POWER_ACTIVE) {
            wakeDisplay();
            return true; // Indicates display was awakened
        }
        return false;
    }

    // ── Display Power State Machine ──────────────────────────────
    DisplayPowerState getDisplayState() const {
        return _displayState;
    }

    void wakeDisplay() {
        if (_displayState == DISPLAY_POWER_ACTIVE) return;

        bool wasAsleep = (_displayState == DISPLAY_POWER_OFF || _displayState == DISPLAY_POWER_SLEEP);
        _displayState = DISPLAY_POWER_ACTIVE;
        _lastActivityMs = millis();

        if (wasAsleep) {
            displayWake();
            if (_wakeCb) _wakeCb();
        }

        int userBr = getUserBrightnessPercent();
        _targetDuty = percentToDuty(userBr);
        _currentDuty = _targetDuty;
        setBacklight(_currentDuty);

        Serial.printf("[power] Display ACTIVE (brightness=%d%%)\n", userBr);
    }

    void dimDisplay() {
        if (_displayState == DISPLAY_POWER_DIMMED || _displayState == DISPLAY_POWER_OFF) return;
        if (hasLock(POWER_LOCK_DISPLAY)) return;

        _displayState = DISPLAY_POWER_DIMMED;
        int userBr = getUserBrightnessPercent();
        int dimmedPct = max(5, userBr / 4);
        _targetDuty = percentToDuty(dimmedPct);

        Serial.printf("[power] Display DIMMED (brightness=%d%%)\n", dimmedPct);
    }

    void sleepDisplay() {
        if (_displayState == DISPLAY_POWER_OFF) return;
        if (hasLock(POWER_LOCK_DISPLAY)) return;

        _displayState = DISPLAY_POWER_OFF;
        _targetDuty = 0;
        _currentDuty = 0;
        setBacklight(0);
        displaySleep();

        // If no audio is playing and no LED lock held, shut off LEDs to save power
        if (!hasLock(POWER_LOCK_AUDIO) && !hasLock(POWER_LOCK_LEDS)) {
            turnOffLEDs();
        }

        Serial.println("[power] Display SLEEP / OFF");
    }

    void toggleScreen() {
        if (_displayState == DISPLAY_POWER_OFF || _displayState == DISPLAY_POWER_SLEEP) {
            wakeDisplay();
        } else {
            sleepDisplay();
        }
    }

    // ── Graceful Shutdown Sequence ───────────────────────────────
    void powerOff(bool restart = false) {
        Serial.printf("[power] Starting graceful %s...\n", restart ? "reboot" : "power-off");

        // 1. Flush debounced volume and settings to NVS
        if (_audio) _audio->flushVolume();
        Preferences p;
        p.begin("poko", false);
        p.putBool("clean_shutdown", true);
        p.end();

        // 2. Stop audio pipelines and put hardware into pop-free standby
        if (_audio) _audio->stopAll();
        standbyAudioOutputHardware();

        // 3. Turn off LEDs
        turnOffLEDs();

        // 4. Put display to sleep
        _targetDuty = 0;
        _currentDuty = 0;
        setBacklight(0);
        displaySleep();

        // 5. Shutdown Wi-Fi radio cleanly
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(50);

        if (restart) {
            Serial.println("[power] Restarting ESP32-S3 now");
            ESP.restart();
            return;
        }

        // 6. Release hardware power latch (BAT_EN = LOW)
        Serial.println("[power] Releasing hardware power latch (BAT_EN=LOW)");
        powerLatchOff();
        delay(100);

        // 7. If USB power is attached, the board remains powered.
        // Fallback into ESP32-S3 Deep Sleep with wake-up on PWR button (GPIO 5) or BOOT (GPIO 0).
        Serial.println("[power] USB power active or fallback; entering Deep Sleep with GPIO wake...");
        esp_sleep_enable_ext1_wakeup((1ULL << POKO_PIN_BTN_PWR) | (1ULL << POKO_PIN_BTN_DOWN), ESP_EXT1_WAKEUP_ANY_LOW);
        esp_deep_sleep_start();
    }

    // ── Main Update Loop ─────────────────────────────────────────
    void update() {
        uint32_t now = millis();

        // 1. Update Battery Monitor
        if (_battery) {
            _battery->update();

            // USB plug-in auto-wake: when plugged in while screen is off, turn screen on immediately
            if (_battery->consumePluggedInEvent()) {
                Serial.println("[power] USB plugged in -> waking display");
                wakeDisplay();
            }

            // Hardware Battery Protection: Cut off power if cell voltage < 3.25V sustained (only if battery actually present!)
            if (_battery->isPresent() && _battery->isCritical()) {
                Serial.println("[power] CRITICAL BATTERY VOLTAGE (<3.25V)! Shutting down immediately to protect cell.");
                powerOff(false);
                return;
            }
        }

        // 1b. CPU Frequency Scaling — 240 MHz on USB/charging (MaxPerf), 160 MHz on battery
        {
            bool wantMax = isMaxPerfActive() || hasLock(POWER_LOCK_OTA) || hasLock(POWER_LOCK_REALTIME_NET);
            uint32_t targetMhz = wantMax ? 240 : 160;
            if (getCpuFrequencyMhz() != targetMhz) {
                setCpuFrequencyMhz(targetMhz);
                Serial.printf("[power] CPU -> %u MHz (%s)\n", targetMhz, wantMax ? "MaxPerf/USB" : "Battery/Managed");
            }
        }

        // 2. Audio Subsystem Lock & Speaker PA Coordination (Session vs Active Rendering)
        if (_audio) {
            AudioSource src = _audio->activeSource();
            bool isRendering = _audio->isSoundPlaying();

            if (isRendering) {
                _lastAudioActiveMs = now;
                acquireLock(POWER_LOCK_AUDIO);

                // SSync and Video only lock low-latency Wi-Fi while actively rendering
                if (src == AUDIO_SSYNC || src == AUDIO_VIDEO) {
                    acquireLock(POWER_LOCK_REALTIME_NET);
                }

                // If speaker amp was in standby, wake it up cleanly
                if (_paStandbyDone || !isSpeakerAmpEnabled()) {
                    restoreAudioOutputHardware();
                    _paStandbyDone = false;
                }
            } else {
                // Audio is not actively rendering: allow Wi-Fi modem sleep on battery
                releaseLock(POWER_LOCK_REALTIME_NET);

                if (src == AUDIO_NONE) {
                    releaseLock(POWER_LOCK_AUDIO);
                }

                // Put speaker amp to standby after 3 seconds of continuous audio idle
                if (!_paStandbyDone && (now - _lastAudioActiveMs >= 3000)) {
                    standbyAudioOutputHardware();
                    _paStandbyDone = true;
                    Serial.println("[power] Audio idle 3s -> speaker amp standby");
                }
            }
        }

        // 3. Dynamic Wi-Fi Power Management (Modem Sleep)
        if (WiFi.status() == WL_CONNECTED) {
            bool perfOverride = isMaxPerfActive() || hasLock(POWER_LOCK_REALTIME_NET) || hasLock(POWER_LOCK_OTA);
            if (perfOverride) {
                // Keep radio fully awake with zero sleep jitter
                if (_wifiSleepEnabled) {
                    WiFi.setSleep(false);
                    _wifiSleepEnabled = false;
                    Serial.println("[power] WiFi modem sleep DISABLED (MaxPerf/Realtime active)");
                }
            } else {
                // Enable 802.11 modem sleep (saves 50-70mA while keeping socket connection)
                bool allowSleep = _prefs ? _prefs->getBool("wifi_sleep", true) : true;
                if (allowSleep && !_wifiSleepEnabled) {
                    WiFi.setSleep(true);
                    _wifiSleepEnabled = true;
                    Serial.println("[power] WiFi modem sleep ENABLED (idle/battery)");
                }
            }
        }

        // 4. Non-blocking Smooth Backlight Ramp
        if (_currentDuty != _targetDuty && (now - _lastDimStepMs >= 10)) {
            _lastDimStepMs = now;
            if (_currentDuty < _targetDuty) {
                int next = _currentDuty + 8;
                _currentDuty = (next >= _targetDuty) ? _targetDuty : (uint8_t)next;
            } else {
                int next = _currentDuty - 8;
                _currentDuty = (next <= _targetDuty) ? _targetDuty : (uint8_t)next;
            }
            setBacklight(_currentDuty);
        }

        // 5. Inactivity State Machine
        if (!hasLock(POWER_LOCK_DISPLAY) && !hasLock(POWER_LOCK_OTA)) {
            uint32_t idleMs = now - _lastActivityMs;

            if (_displayState == DISPLAY_POWER_ACTIVE) {
                if (_dimTimeoutSec > 0 && idleMs >= (_dimTimeoutSec * 1000)) {
                    dimDisplay();
                }
            } else if (_displayState == DISPLAY_POWER_DIMMED) {
                if (_sleepTimeoutSec > 0 && idleMs >= (_sleepTimeoutSec * 1000)) {
                    // If ambient clock is enabled, keep clock dimmed rather than sleeping
                    if (!_ambientClockEnabled) {
                        sleepDisplay();
                    }
                }
            }
        }

        // 6. Auto Power-Off (when idle on battery with no audio playing)
        if (_autoOffSec > 0 && !hasLock(POWER_LOCK_AUDIO) && !hasLock(POWER_LOCK_PREVENT_DEEP_SLEEP) && !hasLock(POWER_LOCK_OTA)) {
            // Only auto power-off if battery is present and not charging
            if (isBatteryPresent() && !_battery->isCharging()) {
                uint32_t idleMs = now - _lastActivityMs;
                if (idleMs >= (_autoOffSec * 1000)) {
                    Serial.printf("[power] Inactivity timeout reached (%u sec on battery) -> Auto Power-Off\n", _autoOffSec);
                    powerOff(false);
                }
            }
        }
    }

    // ── Setters & Getters for Settings / API ─────────────────────
    void setDimTimeout(uint32_t sec) {
        _dimTimeoutSec = sec;
        if (_prefs) _prefs->putUInt("dim_timeout", sec);
    }
    uint32_t getDimTimeout() const { return _dimTimeoutSec; }

    void setSleepTimeout(uint32_t sec) {
        _sleepTimeoutSec = sec;
        if (_prefs) _prefs->putUInt("sleep_timeout", sec);
    }
    uint32_t getSleepTimeout() const { return _sleepTimeoutSec; }

    void setAutoOffTimeout(uint32_t sec) {
        _autoOffSec = sec;
        if (_prefs) _prefs->putUInt("auto_off", sec);
    }
    uint32_t getAutoOffTimeout() const { return _autoOffSec; }

    void setAmbientClock(bool en) {
        _ambientClockEnabled = en;
        if (_prefs) _prefs->putBool("ambient_clock", en);
    }
    bool isAmbientClock() const { return _ambientClockEnabled; }

    uint32_t getIdleSeconds() const {
        return (millis() - _lastActivityMs) / 1000;
    }

    // ── Telemetry JSON Builder for /api/power ───────────────────
    String getTelemetryJson() const {
        String json = "{";
        json += "\"voltage\":";
        if (_battery) json += String(_battery->getVoltage(), 2);
        else          json += "0.0";
        json += ",\"percentage\":";
        if (_battery) json += String(_battery->getPercentage());
        else          json += "0";
        json += ",\"charging\":";
        json += (_battery && _battery->isCharging()) ? "true" : "false";
        json += ",\"full\":";
        json += (_battery && _battery->isFull()) ? "true" : "false";
        json += ",\"low\":";
        json += (_battery && _battery->isLow()) ? "true" : "false";
        json += ",\"critical\":";
        json += (_battery && _battery->isCritical()) ? "true" : "false";
        json += ",\"battery_present\":";
        json += isBatteryPresent() ? "true" : "false";
        json += ",\"usb_powered\":";
        json += isUsbPowered() ? "true" : "false";
        json += ",\"usb_perf_max\":";
        json += _usbPerfMax ? "true" : "false";

        json += ",\"display_state\":";
        switch (_displayState) {
            case DISPLAY_POWER_ACTIVE: json += "\"active\""; break;
            case DISPLAY_POWER_DIMMED: json += "\"dimmed\""; break;
            case DISPLAY_POWER_OFF:
            case DISPLAY_POWER_SLEEP:  json += "\"sleep\""; break;
            default:                   json += "\"unknown\""; break;
        }

        json += ",\"brightness\":";
        json += String(getUserBrightnessPercent());
        json += ",\"backlight_duty\":";
        json += String(_currentDuty);

        json += ",\"locks\":";
        json += String(_locks);

        json += ",\"dim_timeout\":";
        json += String(_dimTimeoutSec);
        json += ",\"sleep_timeout\":";
        json += String(_sleepTimeoutSec);
        json += ",\"auto_off\":";
        json += String(_autoOffSec);
        json += ",\"ambient_clock\":";
        json += _ambientClockEnabled ? "true" : "false";
        json += ",\"idle_sec\":";
        json += String(getIdleSeconds());
        json += ",\"speaker_amp\":";
        json += isSpeakerAmpEnabled() ? "true" : "false";
        json += ",\"wifi_sleep\":";
        json += _wifiSleepEnabled ? "true" : "false";

        json += "}";
        return json;
    }
};
