#include "PowerManager.h"
#include <esp_task_wdt.h>

void PowerManager::_recalcLocks() {
        uint32_t combined = 0;
        for (uint8_t i = 0; i < LOCK_OWNER_COUNT; i++) {
            combined |= _ownerLocks[i];
        }
        _locks = combined;
    }

void PowerManager::setWakeCallback(std::function<void()> cb) { _wakeCb = cb; }

uint8_t PowerManager::percentToDuty(int pct) const {
        pct = constrain(pct, 0, 100);
        return (uint8_t)((pct * 255) / 100);
    }

int PowerManager::getUserBrightnessPercent() const {
        return _userBrightnessPercent;
    }

bool PowerManager::isBatteryPresent() const {
        return _battery && _battery->isPresent();
    }

bool PowerManager::isUsbPowered() const {
        return !isBatteryPresent() || (_battery && _battery->isCharging());
    }

bool PowerManager::isUsbPerfMax() const {
        return _usbPerfMax;
    }

void PowerManager::setUsbPerfMax(bool en) {
        _usbPerfMax = en;
        if (_prefs) _prefs->putBool("usb_perf", en);
        if (_usbPerfMax && isUsbPowered()) {
            WiFi.setSleep(false);
            _wifiSleepEnabled = false;
        }
    }

bool PowerManager::isMaxPerfActive() const {
        return !isBatteryPresent() || (isUsbPowered() && _usbPerfMax);
    }

bool PowerManager::isAmbientClockEnabled() const {
        return _ambientClockEnabled;
    }

PowerManager::PowerManager(BatteryManager* bat, AudioManager* audio, Preferences* prf)
        : _battery(bat), _audio(audio), _prefs(prf) {
        _lockMutex = xSemaphoreCreateMutex();
    }

void PowerManager::begin() {
        _lastActivityMs = millis();
        _lastAudioActiveMs = millis();

        if (_prefs) {
            _dimTimeoutSec = _prefs->getUInt("dim_timeout", 15);
            _sleepTimeoutSec = _prefs->getUInt("sleep_timeout", 30);
            _autoOffSec = _prefs->getUInt("auto_off", 900);
            _autoOffBatteryPct = constrain(_prefs->getUChar("auto_off_pct", 5), 0, 100);
            _ambientClockEnabled = _prefs->getBool("ambient_clock", false);
            _usbPerfMax = _prefs->getBool("usb_perf", true);
            _userBrightnessPercent = constrain(_prefs->getInt("brightness", 80), 1, 100);
        }

        int userBr = getUserBrightnessPercent();
        _targetDuty = percentToDuty(userBr);
        _currentDuty = _targetDuty;
        setBacklight(_currentDuty);

        Serial.printf("[power] PowerManager initialized (dim=%us, sleep=%us, auto_off=%us at <=%u%%, usb_perf=%s)\n",
                      _dimTimeoutSec, _sleepTimeoutSec, _autoOffSec, _autoOffBatteryPct,
                      _usbPerfMax ? "MaxPerf" : "Managed");
    }

void PowerManager::acquireLock(uint32_t mask, PowerLockOwner owner) {
        if (_lockMutex) xSemaphoreTake(_lockMutex, portMAX_DELAY);
        if ((uint8_t)owner < LOCK_OWNER_COUNT) {
            _ownerLocks[owner] |= mask;
        }
        _recalcLocks();
        if (_lockMutex) xSemaphoreGive(_lockMutex);
    }

void PowerManager::releaseLock(uint32_t mask, PowerLockOwner owner) {
        if (_lockMutex) xSemaphoreTake(_lockMutex, portMAX_DELAY);
        if ((uint8_t)owner < LOCK_OWNER_COUNT) {
            _ownerLocks[owner] &= ~mask;
        }
        _recalcLocks();
        if (_lockMutex) xSemaphoreGive(_lockMutex);
    }

bool PowerManager::hasLock(uint32_t mask) const {
        return (_locks & mask) != 0;
    }

uint32_t PowerManager::getLocks() const {
        return _locks;
    }

bool PowerManager::notifyUserActivity(ActivitySource src) {
        _lastActivityMs = millis();

        if (_displayState != DISPLAY_POWER_ACTIVE) {
            wakeDisplay();
            return true; // Indicates display was awakened
        }
        return false;
    }

DisplayPowerState PowerManager::getDisplayState() const {
        return _displayState;
    }

void PowerManager::wakeDisplay() {
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

void PowerManager::dimDisplay() {
        if (_displayState == DISPLAY_POWER_DIMMED || _displayState == DISPLAY_POWER_OFF) return;
        if (hasLock(POWER_LOCK_DISPLAY)) return;

        _displayState = DISPLAY_POWER_DIMMED;
        int userBr = getUserBrightnessPercent();
        int dimmedPct = max(5, userBr / 4);
        _targetDuty = percentToDuty(dimmedPct);

        Serial.printf("[power] Display DIMMED (brightness=%d%%)\n", dimmedPct);
    }

void PowerManager::sleepDisplay() {
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

void PowerManager::toggleScreen() {
        if (_displayState == DISPLAY_POWER_OFF || _displayState == DISPLAY_POWER_SLEEP) {
            wakeDisplay();
        } else {
            sleepDisplay();
        }
    }

void PowerManager::powerOff(bool restart) {
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
        // USB fallback: all three active-low RTC GPIO buttons can wake the chip.
        Serial.println("[power] USB power active or fallback; entering Deep Sleep with GPIO wake...");
        // Wait for the shutdown gesture to release, otherwise ANY_LOW wakes
        // immediately. RTC pull-ups keep the active-low inputs stable in sleep.
        uint32_t releaseStart = millis();
        while ((digitalRead(POKO_PIN_BTN_PWR) == LOW || digitalRead(POKO_PIN_BTN_DOWN) == LOW ||
                digitalRead(POKO_PIN_BTN_UP) == LOW) && millis() - releaseStart < 3000) {
            esp_task_wdt_reset();
            delay(10);
        }
        uint64_t wakeMask = 0;
        for (int pin : {POKO_PIN_BTN_PWR, POKO_PIN_BTN_DOWN, POKO_PIN_BTN_UP}) {
            rtc_gpio_pullup_en((gpio_num_t)pin);
            rtc_gpio_pulldown_dis((gpio_num_t)pin);
            if (digitalRead(pin) != LOW) wakeMask |= 1ULL << pin;
        }
        if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
        esp_deep_sleep_start();
    }

void PowerManager::update() {
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
                acquireLock(POWER_LOCK_AUDIO, LOCK_OWNER_AUDIO);

                // SSync and Video only lock low-latency Wi-Fi while actively rendering
                if (src == AUDIO_SSYNC || src == AUDIO_VIDEO) {
                    acquireLock(POWER_LOCK_REALTIME_NET, LOCK_OWNER_AUDIO);
                }

                // If speaker amp was in standby, wake it up cleanly
                if (_paStandbyDone || !isSpeakerAmpEnabled()) {
                    restoreAudioOutputHardware();
                    _paStandbyDone = false;
                }
            } else {
                // Audio is not actively rendering: allow Wi-Fi modem sleep on battery
                releaseLock(POWER_LOCK_REALTIME_NET, LOCK_OWNER_AUDIO);

                releaseLock(POWER_LOCK_AUDIO, LOCK_OWNER_AUDIO);

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

        // 6. Low-battery Auto Power-Off. Media apps always inhibit this timer.
        int batteryPct = (_battery && isBatteryPresent()) ? _battery->getPercentage() : -1;
        bool shutdownLocked = hasLock(POWER_LOCK_PREVENT_DEEP_SLEEP) || hasLock(POWER_LOCK_OTA);
        bool eligible = _autoOffSec > 0 && _autoOffBatteryPct > 0 && isBatteryPresent() &&
                        !_battery->isCharging() && !_mediaAppActive && !shutdownLocked &&
                        batteryPct >= 0 && batteryPct <= _autoOffBatteryPct;
        if (!eligible) {
            _lowBatteryTiming = false;
        } else {
            if (!_lowBatteryTiming) {
                _lowBatteryTiming = true;
                _lowBatterySinceMs = now;
                Serial.printf("[power] Battery at %d%% (threshold %u%%); auto-off timer started\n",
                              batteryPct, _autoOffBatteryPct);
            }
            uint32_t lowBatteryMs = now - _lowBatterySinceMs;
            if (shouldAutoPowerOff(_autoOffSec, _autoOffBatteryPct, batteryPct,
                                   true, false, _mediaAppActive, shutdownLocked, lowBatteryMs)) {
                Serial.printf("[power] Battery <= %u%% for %u sec -> Auto Power-Off\n",
                              _autoOffBatteryPct, _autoOffSec);
                powerOff(false);
            }
        }
    }

void PowerManager::setBrightnessPercent(int pct, bool persist) {
        pct = constrain(pct, 1, 100);
        _userBrightnessPercent = pct;
        if (persist && _prefs) _prefs->putInt("brightness", pct);
        if (_displayState == DISPLAY_POWER_ACTIVE) {
            _targetDuty = percentToDuty(pct);
            _currentDuty = _targetDuty;
            setBacklight(_currentDuty);
        } else if (_displayState == DISPLAY_POWER_DIMMED) {
            int dimmedPct = max(5, pct / 4);
            _targetDuty = percentToDuty(dimmedPct);
        }
    }

void PowerManager::setDimTimeout(uint32_t sec, bool persist) {
        _dimTimeoutSec = sec;
        if (persist && _prefs) _prefs->putUInt("dim_timeout", sec);
    }

uint32_t PowerManager::getDimTimeout() const { return _dimTimeoutSec; }

void PowerManager::setSleepTimeout(uint32_t sec, bool persist) {
        _sleepTimeoutSec = sec;
        if (persist && _prefs) _prefs->putUInt("sleep_timeout", sec);
    }

uint32_t PowerManager::getSleepTimeout() const { return _sleepTimeoutSec; }

void PowerManager::setAutoOffTimeout(uint32_t sec, bool persist) {
        _autoOffSec = sec;
        if (persist && _prefs) _prefs->putUInt("auto_off", sec);
    }

uint32_t PowerManager::getAutoOffTimeout() const { return _autoOffSec; }

void PowerManager::setAutoOffBatteryPercent(uint8_t percent, bool persist) {
        _autoOffBatteryPct = constrain(percent, 0, 100);
        _lowBatteryTiming = false;
        if (persist && _prefs) _prefs->putUChar("auto_off_pct", _autoOffBatteryPct);
    }

uint8_t PowerManager::getAutoOffBatteryPercent() const { return _autoOffBatteryPct; }

void PowerManager::setMediaAppActive(bool active) {
        if (_mediaAppActive != active) _lowBatteryTiming = false;
        _mediaAppActive = active;
    }

bool PowerManager::isMediaAppActive() const { return _mediaAppActive; }

void PowerManager::setAmbientClock(bool en) {
        _ambientClockEnabled = en;
        if (_prefs) _prefs->putBool("ambient_clock", en);
    }

bool PowerManager::isAmbientClock() const { return _ambientClockEnabled; }

uint32_t PowerManager::getIdleSeconds() const {
        return (millis() - _lastActivityMs) / 1000;
    }

String PowerManager::getTelemetryJson() const {
        String json = "{";
        json += "\"voltage\":";
        if (_battery) json += String(_battery->getVoltage(), 2);
        else          json += "0.0";
        json += ",\"percentage\":";
        if (_battery) json += String(_battery->getPercentage());
        else          json += "-1";
        json += ",\"percentage_source\":\"voltage_estimate\"";
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
        json += (!isBatteryPresent() || (_battery && _battery->isCharging())) ? "true" : "null";
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
        json += ",\"auto_off_battery_pct\":";
        json += String(_autoOffBatteryPct);
        json += ",\"media_app_active\":";
        json += _mediaAppActive ? "true" : "false";
        json += ",\"ambient_clock\":";
        json += _ambientClockEnabled ? "true" : "false";
        json += ",\"idle_sec\":";
        json += String(getIdleSeconds());
        json += ",\"speaker_amp\":";
        json += isSpeakerAmpEnabled() ? "true" : "false";
        json += ",\"wifi_sleep\":";
        json += _wifiSleepEnabled ? "true" : "false";
        json += ",\"wifi_sleep_allowed\":";
        json += (_prefs && _prefs->getBool("wifi_sleep", true)) ? "true" : "false";

        json += "}";
        return json;
    }
