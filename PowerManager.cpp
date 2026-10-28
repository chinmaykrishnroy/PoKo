#include "PowerManager.h"
#include <esp_task_wdt.h>

static constexpr uint32_t IDLE_POWEROFF_MAGIC = 0x504F4B4FU;
RTC_DATA_ATTR static uint32_t pendingIdlePowerOff = 0;

static const char* protectedKindName(ProtectedActivityKind kind) {
        switch (kind) {
            case PROTECTED_SSYNC: return "SSYNC";
            case PROTECTED_MUSIC: return "MUSIC";
            case PROTECTED_VIDEO: return "VIDEO";
            case PROTECTED_TORCH: return "TORCH";
            default: return "NONE";
        }
    }

static uint64_t buttonWakeMask() {
        uint64_t mask = 0;
        for (int pin : {POKO_PIN_BTN_PWR, POKO_PIN_BTN_DOWN, POKO_PIN_BTN_UP}) {
            pinMode(pin, INPUT_PULLUP);
            rtc_gpio_pullup_en((gpio_num_t)pin);
            rtc_gpio_pulldown_dis((gpio_num_t)pin);
            if (digitalRead(pin) != LOW) mask |= 1ULL << pin;
        }
        return mask;
    }

bool completePendingIdlePowerOffWake() {
        bool timerPowerOff = pendingIdlePowerOff == IDLE_POWEROFF_MAGIC &&
                             esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
        pendingIdlePowerOff = 0;
        if (!timerPowerOff) return false;

        powerLatchOff();
        uint64_t wakeMask = buttonWakeMask();
        if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
        esp_deep_sleep_start();
        return true;
    }

void PowerManager::_recalcLocks() {
        uint32_t combined = 0;
        for (uint8_t i = 0; i < LOCK_OWNER_COUNT; i++) {
            combined |= _ownerLocks[i];
        }
        _locks.store(combined, std::memory_order_release);
    }

void PowerManager::setWakeCallback(std::function<void()> cb) { _wakeCb = cb; }

void PowerManager::setNetworkCallbacks(std::function<void()> hibernateCb,
                                       std::function<void()> wakeCb) {
        _wifiHibernateCb = hibernateCb;
        _networkWakeCb = wakeCb;
    }

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

bool PowerManager::hasSustainedPowerForMaxPerformance() const {
        // CHG_STAT proves only that charging is active right now. It is useful
        // for this performance decision, but is not USB/VBUS detection.
        return !isBatteryPresent() || (_battery && _battery->isCharging());
    }

bool PowerManager::isUsbPerfMax() const {
        return _usbPerfMax;
    }

void PowerManager::setUsbPerfMax(bool en) {
        _usbPerfMax = en;
        if (_prefs) _prefs->putBool("usb_perf", en);
        if (_usbPerfMax && hasSustainedPowerForMaxPerformance()) {
            WiFi.setSleep(false);
            _wifiSleepEnabled = false;
        }
    }

void PowerManager::setWifiSleepAllowed(bool en, bool persist) {
        if (persist && _prefs) _prefs->putBool("wifi_sleep", en);
        bool perfOverride = isMaxPerfActive() || hasLock(POWER_LOCK_REALTIME_NET) ||
                            hasLock(POWER_LOCK_OTA);
        bool enableNow = en && !perfOverride && WiFi.status() == WL_CONNECTED;
        WiFi.setSleep(enableNow);
        _wifiSleepEnabled = enableNow;
    }

bool PowerManager::isWifiSleepAllowed() const {
        return _prefs ? _prefs->getBool("wifi_sleep", true) : true;
    }

void PowerManager::setWifiHibernated(bool hibernated) {
        _wifiHibernated = hibernated;
    }

bool PowerManager::isWifiHibernated() const { return _wifiHibernated; }

void PowerManager::setWifiHibernateTimeout(uint32_t sec, bool persist) {
        _wifiHibernateSec = sec > uint32_t{604800} ? uint32_t{604800} : sec;
        if (persist && _prefs) _prefs->putUInt("wifi_hib", _wifiHibernateSec);
    }

uint32_t PowerManager::getWifiHibernateTimeout() const { return _wifiHibernateSec; }

void PowerManager::setLightSleepTimeout(uint32_t sec, bool persist) {
        _lightSleepSec = sec > uint32_t{604800} ? uint32_t{604800} : sec;
        if (persist && _prefs) _prefs->putUInt("light_sleep", _lightSleepSec);
    }

uint32_t PowerManager::getLightSleepTimeout() const { return _lightSleepSec; }

void PowerManager::setDeepSleepTimeout(uint32_t sec, bool persist) {
        _deepSleepSec = sec > uint32_t{604800} ? uint32_t{604800} : sec;
        if (persist && _prefs) _prefs->putUInt("deep_sleep", _deepSleepSec);
    }

uint32_t PowerManager::getDeepSleepTimeout() const { return _deepSleepSec; }

void PowerManager::setIdleCpuMhz(uint8_t mhz, bool persist) {
        _idleCpuMhz = mhz >= 160 ? 160 : 80;
        if (persist && _prefs) _prefs->putUChar("idle_cpu", _idleCpuMhz);
    }

uint8_t PowerManager::getIdleCpuMhz() const { return _idleCpuMhz; }

void PowerManager::setLoopYieldMs(uint8_t ms, bool persist) {
        _loopYieldMs = constrain(ms, 0, 20);
        if (persist && _prefs) _prefs->putUChar("loop_yield", _loopYieldMs);
    }

uint8_t PowerManager::getLoopYieldMs() const { return _loopYieldMs; }

uint8_t PowerManager::recommendedLoopYieldMs() const {
        if (_protectedActivity ||
            hasLock(POWER_LOCK_AUDIO | POWER_LOCK_REALTIME_NET | POWER_LOCK_OTA)) {
            return 0;
        }
        return _loopYieldMs;
    }

bool PowerManager::isMaxPerfActive() const {
        return _usbPerfMax && hasSustainedPowerForMaxPerformance();
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
        _lastProtectedActivityMs = _lastActivityMs;
        _lastAudioActiveMs = millis();

        if (_prefs) {
            _dimTimeoutSec = _prefs->getUInt("dim_timeout", 15);
            _sleepTimeoutSec = _prefs->getUInt("sleep_timeout", 30);
            _autoOffSec = _prefs->getUInt("auto_off", 900);
            _autoOffBatteryPct = constrain(_prefs->getUChar("auto_off_pct", 5), 0, 100);
            _ambientClockEnabled = _prefs->getBool("ambient_clock", false);
            _usbPerfMax = _prefs->getBool("usb_perf", true);
            _wifiHibernateSec = _prefs->getUInt("wifi_hib", 120);
            _lightSleepSec = _prefs->getUInt("light_sleep", 120);
            _deepSleepSec = _prefs->getUInt("deep_sleep", 600);
            _idleCpuMhz = _prefs->getUChar("idle_cpu", 80) >= 160 ? 160 : 80;
            _loopYieldMs = constrain(_prefs->getUChar("loop_yield", 2), 0, 20);
            _userBrightnessPercent = constrain(_prefs->getInt("brightness", 80), 1, 100);
        }

        int userBr = getUserBrightnessPercent();
        _targetDuty = percentToDuty(userBr);
        _currentDuty = _targetDuty;
        setBacklight(_currentDuty);

        Serial.printf("[power] PowerManager initialized (dim=%us, sleep=%us, auto_off=%us, wifi_hib=%us, light=%us, deep=%us, idle_cpu=%uMHz, yield=%ums, protect_active_above=%u%%, usb_perf=%s)\n",
                      _dimTimeoutSec, _sleepTimeoutSec, _autoOffSec,
                      _wifiHibernateSec, _lightSleepSec, _deepSleepSec,
                      _idleCpuMhz, _loopYieldMs, _autoOffBatteryPct,
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
        return (_locks.load(std::memory_order_acquire) & mask) != 0;
    }

uint32_t PowerManager::getLocks() const {
        return _locks.load(std::memory_order_acquire);
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

        Serial.printf("[power] ACTIVE -> DISPLAY_DIMMED brightness=%d%%\n", dimmedPct);
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

        Serial.printf("[power] DISPLAY -> DISPLAY_OFF locks=%lu protected_media=%s idle_ms=%lu\n",
                      (unsigned long)getLocks(), _protectedActivity ? "true" : "false",
                      (unsigned long)(millis() - _lastActivityMs));
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

        // 2. Stop audio pipelines and fully power down codec/I2S
        if (_audio) _audio->stopAll();
        shutdownAudioOutputHardware();

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
        Serial.println("[power] Latch released; entering Deep Sleep with GPIO wake fallback");
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

void PowerManager::_enterLightSleep(uint32_t idleMs) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        uint64_t wakeMask = buttonWakeMask();
        if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
        uint32_t wakeMs = nextLightSleepWakeMs(idleMs, _deepSleepSec, _autoOffSec);
        esp_sleep_enable_timer_wakeup((uint64_t)wakeMs * 1000ULL);
        Serial.printf("[power] LIGHT_SLEEP enter reason=idle wake_in_ms=%lu locks=%lu protected_media=%s idle_ms=%lu\n",
                      (unsigned long)wakeMs, (unsigned long)getLocks(),
                      _protectedActivity ? "true" : "false", (unsigned long)idleMs);
        esp_light_sleep_start();

        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        for (int pin : {POKO_PIN_BTN_PWR, POKO_PIN_BTN_DOWN, POKO_PIN_BTN_UP}) {
            rtc_gpio_hold_dis((gpio_num_t)pin);
            rtc_gpio_deinit((gpio_num_t)pin);
            pinMode(pin, INPUT_PULLUP);
        }
        Serial.printf("[power] LIGHT_SLEEP wake cause=%d\n", (int)cause);
        if (cause == ESP_SLEEP_WAKEUP_EXT1) {
            notifyUserActivity(ACTIVITY_BUTTON);
            if (_networkWakeCb) _networkWakeCb();
        }
    }

void PowerManager::_enterDeepIdle(uint32_t idleMs) {
        Serial.printf("[power] DEEP_SLEEP reason=idle_reboot_with_rtc_state locks=%lu protected_media=%s idle_ms=%lu\n",
                      (unsigned long)getLocks(), _protectedActivity ? "true" : "false",
                      (unsigned long)idleMs);
        if (_audio) _audio->stopAll();
        shutdownAudioOutputHardware();
        turnOffLEDs();
        setBacklight(0);
        displaySleep();
        if (_wifiHibernateCb && !_wifiHibernated) _wifiHibernateCb();
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        uint64_t wakeMask = buttonWakeMask();
        if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

        uint64_t autoOffMs = (uint64_t)_autoOffSec * 1000ULL;
        if (_autoOffSec > 0 && autoOffMs > idleMs) {
            pendingIdlePowerOff = IDLE_POWEROFF_MAGIC;
            esp_sleep_enable_timer_wakeup((autoOffMs - idleMs) * 1000ULL);
        } else {
            pendingIdlePowerOff = 0;
        }
        esp_deep_sleep_start();
    }

void PowerManager::update() {
        uint32_t now = millis();
        bool protectedMedia = protectedMediaHeld(
            _protectedActivity, now, _lastProtectedActivityMs);
        uint32_t userIdleMs = now - _lastActivityMs;
        uint32_t powerIdleMs = effectivePowerIdleMs(
            now, _lastActivityMs, _lastProtectedActivityMs);

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

        // 1b. CPU Frequency Scaling — 240 MHz for explicit performance,
        // 160 MHz while interactive, configurable 80/160 MHz while screen-off idle.
        {
            bool wantMax = isMaxPerfActive() || hasLock(POWER_LOCK_OTA) || hasLock(POWER_LOCK_REALTIME_NET);
            bool interactive = _displayState != DISPLAY_POWER_OFF || protectedMedia ||
                               hasLock(POWER_LOCK_DISPLAY | POWER_LOCK_AUDIO);
            uint32_t targetMhz = wantMax ? 240 : (interactive ? 160 : _idleCpuMhz);
            if (getCpuFrequencyMhz() != targetMhz) {
                setCpuFrequencyMhz(targetMhz);
                Serial.printf("[power] CPU -> %u MHz (%s)\n", targetMhz,
                              wantMax ? "MaxPerf/Realtime" : (interactive ? "Interactive" : "Idle"));
            }
        }

        // 2. Audio Subsystem Lock & Speaker PA Coordination (Session vs Active Rendering)
        if (_audio) {
            AudioSource src = _audio->activeSource();
            bool isRendering = _audio->isSoundPlaying();
            if (_audio->hasActiveSession() && poko_tx_handle) {
                _audioHardwareOff = false;
            }

            if (isRendering) {
                _lastAudioActiveMs = now;
            }
            bool keepAudioPowered = isRendering ||
                (_audio->hasActiveSession() &&
                 (now - _lastAudioActiveMs) < 5000);

            if (keepAudioPowered) {
                acquireLock(POWER_LOCK_AUDIO, LOCK_OWNER_AUDIO);

                // SSync and Video only lock low-latency Wi-Fi while actively rendering
                if (src == AUDIO_SSYNC || src == AUDIO_VIDEO) {
                    acquireLock(POWER_LOCK_REALTIME_NET, LOCK_OWNER_AUDIO);
                }

                // If speaker amp was in standby, wake it up cleanly
                if (_paStandbyDone || !isSpeakerAmpEnabled()) {
                    restoreAudioOutputHardware();
                    _paStandbyDone = false;
                    _audioHardwareOff = false;
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
                if (!_audioHardwareOff && !_audio->hasActiveSession() &&
                    (now - _lastAudioActiveMs >= 10000)) {
                    shutdownAudioOutputHardware();
                    _audioHardwareOff = true;
                    Serial.println("[power] Audio idle 10s -> codec and I2S powered down");
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

        // 5. Inactivity State Machine. Dim and sleep are independent deadlines
        // measured from the same user activity timestamp.
        if (!hasLock(POWER_LOCK_DISPLAY) && !hasLock(POWER_LOCK_OTA)) {
            uint32_t idleMs = now - _lastActivityMs;
            DisplayIdleAction action = displayIdleAction(
                idleMs, _dimTimeoutSec, _sleepTimeoutSec, _ambientClockEnabled);
            if (action == DISPLAY_IDLE_SLEEP) {
                sleepDisplay();
            } else if (action == DISPLAY_IDLE_DIM &&
                       _displayState == DISPLAY_POWER_ACTIVE) {
                dimDisplay();
            }
        }

        // 6. Inactivity Auto Power-Off. Active playback or the solid Pixel
        // torch inhibits the timer while external power is present or the
        // battery remains above the configured protection threshold.
        int batteryPct = (_battery && isBatteryPresent()) ? _battery->getPercentage() : -1;
        bool batteryPresent = isBatteryPresent();
        bool charging = _battery && _battery->isCharging();
        bool shutdownLocked = hasLock(POWER_LOCK_PREVENT_DEEP_SLEEP) || hasLock(POWER_LOCK_OTA);
        bool activityInhibits = protectedActivityCanInhibitAutoOff(
            _autoOffBatteryPct, batteryPct, batteryPresent, charging, protectedMedia);
        bool timerInhibited = _autoOffSec == 0 || shutdownLocked || activityInhibits;
        _autoOffTiming = !timerInhibited;
        if (!timerInhibited) {
            uint32_t inactiveMs = protectedMedia ? userIdleMs : powerIdleMs;
            if (shouldAutoPowerOff(_autoOffSec, _autoOffBatteryPct, batteryPct,
                                   batteryPresent, charging, protectedMedia,
                                   shutdownLocked, inactiveMs)) {
                Serial.printf("[power] AUTO_OFF execute battery=%d protected_media=%s idle_ms=%lu\n",
                              batteryPct, protectedMedia ? "true" : "false",
                              (unsigned long)inactiveMs);
                powerOff(false);
            }
        }

        // 7. Display and radio are independent: local apps may keep the display
        // visible while Wi-Fi hibernates (static pink status dot). Light/deep
        // sleep require DISPLAY_POWER_OFF. Protected media blocks all three.
        uint32_t idleMs = powerIdleMs;
        bool blockingLowPowerLock = hasLock(
            POWER_LOCK_DISPLAY | POWER_LOCK_AUDIO | POWER_LOCK_REALTIME_NET |
            POWER_LOCK_OTA | POWER_LOCK_PREVENT_LIGHT_SLEEP |
            POWER_LOCK_PREVENT_DEEP_SLEEP);
        bool displayOff = _displayState == DISPLAY_POWER_OFF;
        if (idlePowerStageDue(_deepSleepSec, idleMs, displayOff,
                              protectedMedia, blockingLowPowerLock)) {
            _enterDeepIdle(idleMs);
            return;
        }
        bool blockingWifiLock = hasLock(POWER_LOCK_AUDIO | POWER_LOCK_REALTIME_NET |
                                        POWER_LOCK_OTA | POWER_LOCK_PREVENT_DEEP_SLEEP);
        if (!_wifiHibernated && _wifiHibernateCb &&
            idlePowerStageDue(_wifiHibernateSec, idleMs, true,
                              protectedMedia, blockingWifiLock)) {
            Serial.printf("[power] WIFI -> HIBERNATE reason=idle locks=%lu protected_media=%s idle_ms=%lu\n",
                          (unsigned long)getLocks(), protectedMedia ? "true" : "false",
                          (unsigned long)idleMs);
            _wifiHibernateCb();
        }
        if (_wifiHibernated &&
            idlePowerStageDue(_lightSleepSec, idleMs, displayOff,
                              protectedMedia, blockingLowPowerLock)) {
            _enterLightSleep(idleMs);
        }

        if (now - _lastPowerTraceMs >= 30000) {
            _lastPowerTraceMs = now;
            if (activityInhibits) {
                Serial.printf("[power] AUTO_OFF blocked_by=%s battery=%d\n",
                              protectedKindName(_protectedKind), batteryPct);
            }
            Serial.printf("[power] locks=%lu protected_media=%s source=%s user_idle_ms=%lu power_idle_ms=%lu wifi_hibernated=%s\n",
                          (unsigned long)getLocks(), protectedMedia ? "true" : "false",
                          protectedKindName(_protectedKind),
                          (unsigned long)userIdleMs, (unsigned long)powerIdleMs,
                          _wifiHibernated ? "true" : "false");
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
        if (persist && _prefs) _prefs->putUChar("auto_off_pct", _autoOffBatteryPct);
    }

uint8_t PowerManager::getAutoOffBatteryPercent() const { return _autoOffBatteryPct; }

void PowerManager::setProtectedActivity(bool active, ProtectedActivityKind kind) {
        if (active) _lastProtectedActivityMs = millis();
        _protectedActivity = active;
        _protectedKind = active ? kind : PROTECTED_NONE;
    }

bool PowerManager::isProtectedActivity() const { return _protectedActivity; }

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
        json += ",\"raw_voltage\":";
        if (_battery) json += String(_battery->getRawVoltage(), 2);
        else          json += "0.0";
        json += ",\"percentage\":";
        if (_battery) json += String(_battery->getPercentage());
        else          json += "-1";
        json += ",\"percentage_source\":\"filtered_voltage_estimate\"";
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
        json += ",\"external_power_presence\":\"";
        json += isBatteryPresent() ? "unknown" : "required_no_battery";
        json += "\"";
        json += ",\"usb_powered\":";
        json += isBatteryPresent() ? "null" : "true";
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
        json += String(getLocks());

        json += ",\"dim_timeout\":";
        json += String(_dimTimeoutSec);
        json += ",\"sleep_timeout\":";
        json += String(_sleepTimeoutSec);
        json += ",\"auto_off\":";
        json += String(_autoOffSec);
        json += ",\"auto_off_battery_pct\":";
        json += String(_autoOffBatteryPct);
        json += ",\"protected_activity\":";
        json += _protectedActivity ? "true" : "false";
        json += ",\"protected_activity_source\":\"";
        json += protectedKindName(_protectedKind);
        json += "\"";
        // Kept for dashboard/client compatibility; semantics now mean actual
        // protected playback or solid-torch activity, not an open media page.
        json += ",\"media_app_active\":";
        json += _protectedActivity ? "true" : "false";
        json += ",\"auto_off_timer_active\":";
        json += _autoOffTiming ? "true" : "false";
        json += ",\"auto_off_elapsed_sec\":";
        json += _autoOffTiming ? String(getIdleSeconds()) : "0";
        json += ",\"ambient_clock\":";
        json += _ambientClockEnabled ? "true" : "false";
        json += ",\"idle_sec\":";
        json += String(getIdleSeconds());
        json += ",\"power_idle_sec\":";
        json += String(effectivePowerIdleMs(millis(), _lastActivityMs,
                                            _lastProtectedActivityMs) / 1000);
        json += ",\"speaker_amp\":";
        json += isSpeakerAmpEnabled() ? "true" : "false";
        json += ",\"wifi_sleep\":";
        json += _wifiSleepEnabled ? "true" : "false";
        json += ",\"wifi_sleep_allowed\":";
        json += isWifiSleepAllowed() ? "true" : "false";
        json += ",\"wifi_hibernated\":";
        json += _wifiHibernated ? "true" : "false";
        json += ",\"wifi_hibernate_timeout\":";
        json += String(_wifiHibernateSec);
        json += ",\"light_sleep_timeout\":";
        json += String(_lightSleepSec);
        json += ",\"deep_sleep_timeout\":";
        json += String(_deepSleepSec);
        json += ",\"idle_cpu_mhz\":";
        json += String(_idleCpuMhz);
        json += ",\"loop_yield_ms\":";
        json += String(_loopYieldMs);

        json += "}";
        return json;
    }
