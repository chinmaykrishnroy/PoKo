#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

#include "PokoTheme.h"
#include "AudioManager.h"
#include "SnapPlayer.h"
#include "PowerManager.h"

// ─────────────────────────────────────────────────────────────
//  SettingsApp — On-device settings menu (128×128)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern void handleDriverReset();
extern SnapPlayer* snapService;
extern PowerManager* powerManager;

class SettingsApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _selected  = 0;
    uint8_t  _scroll    = 0;
    uint32_t _lastBlinkMs = 0;

    static constexpr uint8_t ITEM_COUNT   = 11;
    static constexpr uint8_t ROW_H        = 16;
    static constexpr uint8_t TOP_Y        = 14;
    static constexpr uint8_t ROWS_VISIBLE = 6;
    static constexpr uint8_t FOOTER_Y     = 114;

    const char* _items[ITEM_COUNT] = {
        "Theme",
        "Master Vol",
        "Brightness",
        "Amp Boost",
        "Dim Timeout",
        "Sleep Timeout",
        "Auto-Off",
        "Slide Timer",
        "SSync Auto",
        "Reset Drivers",
        "Reboot"
    };

    void adjustScroll() {
        if (_selected < _scroll) {
            _scroll = _selected;
        } else if (_selected >= _scroll + ROWS_VISIBLE) {
            _scroll = _selected - ROWS_VISIBLE + 1;
        }
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 13, theme.headerBg);
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

        int curBr      = prefs.getInt("brightness", 80);
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
                    if (st == 0)      snprintf(valBuf, sizeof(valBuf), "Off");
                    else if (st < 60) snprintf(valBuf, sizeof(valBuf), "%us", st);
                    else              snprintf(valBuf, sizeof(valBuf), "%um", st / 60);
                    break;
                }
                case 6: {
                    uint32_t ao = powerManager ? powerManager->getAutoOffTimeout() : prefs.getUInt("auto_off", 900);
                    if (ao == 0) snprintf(valBuf, sizeof(valBuf), "Never");
                    else         snprintf(valBuf, sizeof(valBuf), "%um", ao / 60);
                    break;
                }
                case 7:
                    if (curSlide == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else snprintf(valBuf, sizeof(valBuf), "%ds", curSlide);
                    break;
                case 8: snprintf(valBuf, sizeof(valBuf), ssyncAuto ? "On" : "Off"); break;
                case 9: snprintf(valBuf, sizeof(valBuf), "Exec"); valCol = POKO_CLR_WARN; break;
                case 10: snprintf(valBuf, sizeof(valBuf), "Restart"); valCol = POKO_CLR_ERR; break;
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
        const char* hint = "L:Up  R:Dn  2R:Set";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

    void applyAction() {
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
                prefs.putInt("brightness", b);
                setBacklightPercent(b);
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
            case 7: { // Cycle Slide Timer: 0 -> 3 -> 5 -> 10 -> 15 -> 30 -> 60 -> 0
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
            case 8: { // SSync Auto (On <-> Off)
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
            case 9: { // Reset Drivers
                handleDriverReset();
                break;
            }
            case 10: { // Reboot
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

public:
    SettingsApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active   = true;
        _selected = 0;
        _scroll   = 0;
        _dirty    = true;
        begin();
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        _selected = (_selected == 0) ? (ITEM_COUNT - 1) : (_selected - 1);
        adjustScroll();
        _dirty = true;
    }

    void onRight() {
        _selected = (_selected + 1) % ITEM_COUNT;
        adjustScroll();
        _dirty = true;
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        applyAction();
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
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
};
