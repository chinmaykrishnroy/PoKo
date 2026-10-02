#include "SettingsApp.h"
#include <new>

void SettingsApp::adjustScroll() {
        if (_selected < _scroll) {
            _scroll = _selected;
        } else if (_selected >= _scroll + ROWS_VISIBLE) {
            _scroll = _selected - ROWS_VISIBLE + 1;
        }
    }

void SettingsApp::flushHeldValue() {
        switch (_pendingItem) {
            case 1: prefs.putInt("master_vol", getMasterVolumeLimit()); break;
            case 2: if (powerManager) prefs.putInt("brightness", powerManager->getUserBrightnessPercent()); break;
            case 4: if (powerManager) prefs.putUInt("dim_timeout", powerManager->getDimTimeout()); break;
            case 5: if (powerManager) prefs.putUInt("sleep_timeout", powerManager->getSleepTimeout()); break;
            case 6: if (powerManager) prefs.putUInt("auto_off", powerManager->getAutoOffTimeout()); break;
            case 7: if (powerManager) prefs.putUChar("auto_off_pct", powerManager->getAutoOffBatteryPercent()); break;
        }
        _pendingItem = -1;
    }

void SettingsApp::adjustHeldValue(int direction) {
        if (!powerManager || (_selected != 1 && _selected != 2 && _selected != 4 &&
                              _selected != 5 && _selected != 6 && _selected != 7)) return;
        uint32_t now = millis();
        if (_holdDirection != direction || now - _lastHoldMs > 350) _holdStartMs = now;
        _holdDirection = direction;
        _lastHoldMs = now;
        uint32_t held = now - _holdStartMs;
        int percentStep = held < 1000 ? 1 : held < 2500 ? 3 : 8;
        uint32_t secondsStep = held < 1000 ? 5 : held < 2500 ? 30 : 120;
        uint32_t autoOffStep = held < 1000 ? 60 : held < 2500 ? 300 : 1800;
        auto adjustSeconds = [&](uint32_t value, uint32_t step) -> uint32_t {
            const uint32_t maximum = 604800;
            return direction > 0 ? min(value + step, maximum)
                                 : (value > step ? value - step : uint32_t{0});
        };
        if (_selected == 1) {
            setMasterVolumeLimit(constrain(getMasterVolumeLimit() + direction * percentStep, 0, 100));
        } else if (_selected == 2) {
            powerManager->setBrightnessPercent(powerManager->getUserBrightnessPercent() + direction * percentStep, false);
        } else if (_selected == 4) {
            powerManager->setDimTimeout(adjustSeconds(powerManager->getDimTimeout(), secondsStep), false);
        } else if (_selected == 5) {
            powerManager->setSleepTimeout(adjustSeconds(powerManager->getSleepTimeout(), secondsStep), false);
        } else if (_selected == 6) {
            powerManager->setAutoOffTimeout(adjustSeconds(powerManager->getAutoOffTimeout(), autoOffStep), false);
        } else {
            powerManager->setAutoOffBatteryPercent((uint8_t)constrain(
                (int)powerManager->getAutoOffBatteryPercent() + direction * percentStep, 0, 100), false);
        }
        _pendingItem = _selected;
        _dirty = true;
    }

void SettingsApp::renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, 13, 128, theme.line);  // bottom border
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.headerText, theme.headerBg);
        _canvas->setCursor(3, 10);
        _canvas->print("Settings");

        if (audioManager) {
            audioManager->drawStatusDot(_canvas, 52, 6, 2);
        }

        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _selected + 1, ITEM_COUNT);
        _canvas->setTextColor(theme.muted, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 10);
        _canvas->print(countBuf);

        // Menu items area (y=14..113)
        _canvas->fillRect(0, TOP_Y, 128, FOOTER_Y - TOP_Y, theme.bg);
        _canvas->setFont(u8g2_font_profont10_mf);

        int curBr      = powerManager ? powerManager->getUserBrightnessPercent() : prefs.getInt("brightness", 80);
        int curMaster  = getMasterVolumeLimit();
        int curBoost   = getAmpBoostDb();
        int curSlide   = prefs.getInt("gallery_timer", 0);
        bool ssyncAuto = prefs.getBool("snap_auto", true);
        bool isDark    = isDarkTheme();

        for (uint8_t i = 0; i < ROWS_VISIBLE; i++) {
            uint8_t itemIdx = _scroll + i;
            if (itemIdx >= ITEM_COUNT) break;

            int16_t rowY = TOP_Y + 2 + i * ROW_H;
            bool isSel = (itemIdx == _selected);

            if (isSel) {
                _canvas->fillRect(0, rowY - 2, 124, 15, theme.surface);
                _canvas->drawFastHLine(0, rowY - 2, 124, theme.accent);
                _canvas->drawFastHLine(0, rowY + 12, 124, theme.accent);
            }

            _canvas->setTextColor(isSel ? theme.text : theme.muted, isSel ? theme.surface : theme.bg);
            _canvas->setCursor(4, rowY + 9);
            _canvas->print(_items[itemIdx]);

            // Value text on the right
            char valBuf[16] = "";
            uint16_t valCol = isSel ? theme.accent : theme.muted;

            switch (itemIdx) {
                case 0: snprintf(valBuf, sizeof(valBuf), isDark ? "Dark" : "Light"); break;
                case 1: snprintf(valBuf, sizeof(valBuf), "%d%%", curMaster); break;
                case 2: snprintf(valBuf, sizeof(valBuf), "%d%%", curBr); break;
                case 3: snprintf(valBuf, sizeof(valBuf), "+%ddB", curBoost); break;
                case 4: {
                    uint32_t dt = powerManager ? powerManager->getDimTimeout() : prefs.getUInt("dim_timeout", 15);
                    if (dt == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else         snprintf(valBuf, sizeof(valBuf), "%us", dt);
                    break;
                }
                case 5: {
                    uint32_t st = powerManager ? powerManager->getSleepTimeout() : prefs.getUInt("sleep_timeout", 30);
                    if (st == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else         snprintf(valBuf, sizeof(valBuf), "%us", st);
                    break;
                }
                case 6: {
                    uint32_t ao = powerManager ? powerManager->getAutoOffTimeout() : prefs.getUInt("auto_off", 900);
                    if (ao == 0) snprintf(valBuf, sizeof(valBuf), "Never");
                    else         snprintf(valBuf, sizeof(valBuf), "%um", ao / 60);
                    break;
                }
                case 7: {
                    uint8_t pct = powerManager ? powerManager->getAutoOffBatteryPercent() : prefs.getUChar("auto_off_pct", 5);
                    if (pct == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else          snprintf(valBuf, sizeof(valBuf), "%u%%", pct);
                    break;
                }
                case 8: {
                    bool ac = powerManager ? powerManager->isAmbientClockEnabled() : prefs.getBool("ambient_clock", false);
                    snprintf(valBuf, sizeof(valBuf), ac ? "On" : "Off");
                    break;
                }
                case 9: {
                    bool ws = prefs.getBool("wifi_sleep", true);
                    snprintf(valBuf, sizeof(valBuf), ws ? "Auto" : "Off");
                    break;
                }
                case 10: {
                    bool um = powerManager ? powerManager->isUsbPerfMax() : prefs.getBool("usb_perf", true);
                    snprintf(valBuf, sizeof(valBuf), um ? "MaxPerf" : "Managed");
                    break;
                }
                case 11: {
                    if (curSlide == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else snprintf(valBuf, sizeof(valBuf), "%ds", curSlide);
                    break;
                }
                case 12: snprintf(valBuf, sizeof(valBuf), ssyncAuto ? "On" : "Off"); break;
                case 13: {
                    uint8_t br = pixelEngine.getBrightness();
                    if (br == 0)      snprintf(valBuf, sizeof(valBuf), "Off");
                    else if (br <= 3) snprintf(valBuf, sizeof(valBuf), "20%%");
                    else if (br <= 7) snprintf(valBuf, sizeof(valBuf), "50%%");
                    else              snprintf(valBuf, sizeof(valBuf), "100%%");
                    break;
                }
                case 14: snprintf(valBuf, sizeof(valBuf), "Exec"); valCol = POKO_CLR_WARN; break;
                case 15: snprintf(valBuf, sizeof(valBuf), "Shut"); valCol = POKO_CLR_ERR; break;
                case 16: snprintf(valBuf, sizeof(valBuf), "Restart"); valCol = POKO_CLR_ERR; break;
            }

            _canvas->setTextColor(valCol, isSel ? theme.surface : theme.bg);
            _canvas->getTextBounds(valBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(122 - w, rowY + 9);
            _canvas->print(valBuf);
        }

        // Scroll indicator bar
        if (ITEM_COUNT > ROWS_VISIBLE) {
            uint8_t barH = (ROWS_VISIBLE * (FOOTER_Y - TOP_Y)) / ITEM_COUNT;
            uint8_t barY = TOP_Y + (_scroll * (FOOTER_Y - TOP_Y - barH)) / (ITEM_COUNT - ROWS_VISIBLE);
            _canvas->drawFastVLine(126, TOP_Y, FOOTER_Y - TOP_Y, theme.line);
            _canvas->drawFastVLine(126, barY, barH, theme.accent);
        }

        // Footer (y=114..127)
        _canvas->fillRect(0, FOOTER_Y, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, FOOTER_Y, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = (_selected == 1 || _selected == 2 || _selected == 4 ||
                            _selected == 5 || _selected == 6 || _selected == 7) ? "Hold L:- R:+  2R:Set" : "L:Prv  R:Nxt  2R:Set";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

void SettingsApp::applyAction() {
        switch (_selected) {
            case 0: { // Toggle Theme (Dark <-> Light)
                bool nextDark = !isDarkTheme();
                setPokoTheme(nextDark);
                prefs.putString("ui_theme", nextDark ? "dark" : "light");
                break;
            }
            case 1: { // Cycle Master Volume Limit: 20% -> 40% -> 60% -> 80% -> 100% -> 20%
                int mv = getMasterVolumeLimit();
                mv = (mv >= 100) ? 20 : (mv + 20);
                setMasterVolumeLimit(mv);
                prefs.putInt("master_vol", mv);
                break;
            }
            case 2: { // Cycle Brightness: 25% -> 50% -> 75% -> 100% -> 25%
                int b = prefs.getInt("brightness", 80);
                if (b <= 30)      b = 50;
                else if (b <= 60) b = 75;
                else if (b <= 85) b = 100;
                else              b = 25;
                if (powerManager) powerManager->setBrightnessPercent(b);
                else {
                    prefs.putInt("brightness", b);
                    setBacklightPercent(b);
                }
                break;
            }
            case 3: { // Cycle Amp Boost: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0
                int boost = getAmpBoostDb();
                boost = (boost >= 5) ? 0 : (boost + 1);
                setAmpBoostDb(boost);
                prefs.putInt("amp_boost", boost);
                break;
            }
            case 4: { // Dim Timeout: 15s -> 30s -> 60s -> Off(0) -> 15s
                uint32_t cur = powerManager ? powerManager->getDimTimeout() : prefs.getUInt("dim_timeout", 15);
                uint32_t next = (cur == 15) ? 30 : ((cur == 30) ? 60 : ((cur == 60) ? 0 : 15));
                if (powerManager) powerManager->setDimTimeout(next);
                else              prefs.putUInt("dim_timeout", next);
                break;
            }
            case 5: { // Sleep Timeout: 30s -> 1m(60s) -> 2m(120s) -> 5m(300s) -> Off(0) -> 30s
                uint32_t cur = powerManager ? powerManager->getSleepTimeout() : prefs.getUInt("sleep_timeout", 30);
                uint32_t next = 30;
                if (cur == 30)       next = 60;
                else if (cur == 60)  next = 120;
                else if (cur == 120) next = 300;
                else if (cur == 300) next = 0;
                else                 next = 30;
                if (powerManager) powerManager->setSleepTimeout(next);
                else              prefs.putUInt("sleep_timeout", next);
                break;
            }
            case 6: { // Auto-Off: 10m(600s) -> 15m(900s) -> 30m(1800s) -> Never(0) -> 10m(600s)
                uint32_t cur = powerManager ? powerManager->getAutoOffTimeout() : prefs.getUInt("auto_off", 900);
                uint32_t next = 600;
                if (cur == 600)       next = 900;
                else if (cur == 900)  next = 1800;
                else if (cur == 1800) next = 0;
                else                  next = 600;
                if (powerManager) powerManager->setAutoOffTimeout(next);
                else              prefs.putUInt("auto_off", next);
                break;
            }
            case 7: { // Low-battery threshold: 3% -> 5% -> 10% -> Off -> 3%
                uint8_t cur = powerManager ? powerManager->getAutoOffBatteryPercent() : prefs.getUChar("auto_off_pct", 5);
                uint8_t next = cur == 3 ? 5 : (cur == 5 ? 10 : (cur == 10 ? 0 : 3));
                if (powerManager) powerManager->setAutoOffBatteryPercent(next);
                else              prefs.putUChar("auto_off_pct", next);
                break;
            }
            case 8: { // Ambient Clock (On <-> Off)
                bool cur = powerManager ? powerManager->isAmbientClockEnabled() : prefs.getBool("ambient_clock", false);
                bool next = !cur;
                if (powerManager) powerManager->setAmbientClock(next);
                else              prefs.putBool("ambient_clock", next);
                break;
            }
            case 9: { // WiFi Sleep (Auto <-> Off)
                bool cur = prefs.getBool("wifi_sleep", true);
                bool next = !cur;
                prefs.putBool("wifi_sleep", next);
                WiFi.setSleep(next);
                break;
            }
            case 10: { // USB Mode (MaxPerf <-> Managed)
                bool cur = powerManager ? powerManager->isUsbPerfMax() : prefs.getBool("usb_perf", true);
                bool next = !cur;
                if (powerManager) powerManager->setUsbPerfMax(next);
                else              prefs.putBool("usb_perf", next);
                break;
            }
            case 11: { // Cycle Slide Timer: 0 -> 3 -> 5 -> 10 -> 15 -> 30 -> 60 -> 0
                int cur = prefs.getInt("gallery_timer", 0);
                int next = 0;
                if (cur == 0)       next = 3;
                else if (cur <= 3)  next = 5;
                else if (cur <= 5)  next = 10;
                else if (cur <= 10) next = 15;
                else if (cur <= 15) next = 30;
                else if (cur <= 30) next = 60;
                else                next = 0;
                prefs.putInt("gallery_timer", next);
                break;
            }
            case 12: { // SSync Auto (On <-> Off)
                bool nextAuto = !prefs.getBool("snap_auto", true);
                prefs.putBool("snap_auto", nextAuto);
                if (nextAuto) {
                    if (WiFi.status() == WL_CONNECTED && snapService && !snapService->isLoaded()) {
                        if (audioManager && audioManager->activeSource() != AUDIO_NONE) {
                            snapService->load(true);
                            audioManager->setSuspendedSource(AUDIO_SSYNC);
                        } else if (audioManager) {
                            audioManager->request(AUDIO_SSYNC);
                        }
                    }
                } else {
                    if (audioManager) {
                        if (audioManager->activeSource() == AUDIO_SSYNC) {
                            audioManager->stopActiveSession();
                        } else if (audioManager->suspendedSource() == AUDIO_SSYNC) {
                            audioManager->setSuspendedSource(AUDIO_NONE);
                        }
                    }
                    if (snapService && snapService->isLoaded()) {
                        snapService->stop();
                        snapService->unload();
                    }
                }
                break;
            }
            case 13: { // LED Bright (Off -> 20% -> 50% -> 100% -> Off)
                uint8_t curB = pixelEngine.getBrightness();
                uint8_t nextB = 7;
                if (curB == 0)      nextB = 3;
                else if (curB <= 3) nextB = 7;
                else if (curB <= 7) nextB = 15;
                else                nextB = 0;
                pixelEngine.setBrightness(nextB);
                pixelEngine.saveToPreferences(prefs);
                break;
            }
            case 14: { // Reset Drivers
                handleDriverReset();
                break;
            }
            case 15: { // Power Off (Graceful Shutdown)
                if (powerManager) {
                    _canvas->fillScreen(POKO_CLR_ERR);
                    _canvas->setFont(u8g2_font_helvB10_tf);
                    _canvas->setTextColor(POKO_CLR_TEXT);
                    _canvas->setCursor(18, 68);
                    _canvas->print("POWER OFF...");
                    _canvas->flush();
                    delay(400);
                    powerManager->powerOff(false);
                }
                break;
            }
            case 16: { // Reboot
                prefs.putBool("clean_shutdown", true);
                _canvas->fillScreen(POKO_CLR_ERR);
                _canvas->setFont(u8g2_font_helvB10_tf);
                _canvas->setTextColor(POKO_CLR_TEXT);
                _canvas->setCursor(20, 68);
                _canvas->print("REBOOTING...");
                _canvas->flush();
                delay(500);
                ESP.restart();
                break;
            }
        }
        _dirty = true;
    }

SettingsApp::SettingsApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

void SettingsApp::begin() {
        if (!_canvas) {
            _canvas = new (std::nothrow) Arduino_Canvas(128, 128, _gfx, 0, 0);
            if (_canvas) _canvas->begin();
        }
    }

void SettingsApp::load() {
        _active   = true;
        _selected = 0;
        _scroll   = 0;
        _dirty    = true;
        begin();
        renderToCanvas();
    }

void SettingsApp::unload() {
        _active = false;
        flushHeldValue();
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

bool SettingsApp::isLoaded() const { return _active; }

void SettingsApp::refreshTheme() { if (_active) renderToCanvas(); }

void SettingsApp::onLeft() {
        flushHeldValue();
        _selected = (_selected == 0) ? (ITEM_COUNT - 1) : (_selected - 1);
        adjustScroll();
        _dirty = true;
    }

void SettingsApp::onRight() {
        flushHeldValue();
        _selected = (_selected + 1) % ITEM_COUNT;
        adjustScroll();
        _dirty = true;
    }

void SettingsApp::onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

void SettingsApp::onEnter() {
        flushHeldValue();
        applyAction();
    }

void SettingsApp::onHoldingLeft() { adjustHeldValue(-1); }

void SettingsApp::onHoldingRight() { adjustHeldValue(1); }

void SettingsApp::update() {
        if (!_active) return;
        uint32_t now = millis();
        if (_pendingItem >= 0 && now - _lastHoldMs >= 500) flushHeldValue();
        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        if (audioBlinking && (now - _lastBlinkMs >= 100)) {
            _lastBlinkMs = now;
            _dirty = true;
        }
        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
