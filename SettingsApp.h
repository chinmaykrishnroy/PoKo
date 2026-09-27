#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

#include "PokoTheme.h"

// ─────────────────────────────────────────────────────────────
//  SettingsApp — On-device settings menu (128×128)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern void handleDriverReset();

class SettingsApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _selected  = 0;

    static constexpr uint8_t ITEM_COUNT = 7;
    const char* _items[ITEM_COUNT] = {
        "Theme",
        "Master Vol",
        "Brightness",
        "Amp Boost",
        "Slide Timer",
        "Reset Drivers",
        "Reboot"
    };

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 13, theme.headerBg);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.headerText, theme.headerBg);
        _canvas->setCursor(3, 10);
        _canvas->print("Settings");

        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _selected + 1, ITEM_COUNT);
        _canvas->setTextColor(theme.muted, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 10);
        _canvas->print(countBuf);

        // Menu items area (y=14..113)
        _canvas->fillRect(0, 14, 128, 100, theme.bg);
        _canvas->setFont(u8g2_font_profont10_mf);

        int curBr      = prefs.getInt("brightness", 80);
        int curMaster  = getMasterVolumeLimit();
        int curBoost   = getAmpBoostDb();
        int curSlide   = prefs.getInt("gallery_timer", 0);
        bool isDark    = isDarkTheme();

        for (uint8_t i = 0; i < ITEM_COUNT; i++) {
            int16_t rowY = 16 + i * 16;
            bool isSel = (i == _selected);

            if (isSel) {
                _canvas->fillRect(0, rowY - 2, 128, 15, theme.surface);
                _canvas->drawFastHLine(0, rowY - 2, 128, theme.accent);
                _canvas->drawFastHLine(0, rowY + 12, 128, theme.accent);
            }

            _canvas->setTextColor(isSel ? theme.text : theme.muted, isSel ? theme.surface : theme.bg);
            _canvas->setCursor(4, rowY + 9);
            _canvas->print(_items[i]);

            // Value text on the right
            char valBuf[16] = "";
            uint16_t valCol = isSel ? theme.accent : theme.muted;

            switch (i) {
                case 0: snprintf(valBuf, sizeof(valBuf), isDark ? "Dark" : "Light"); break;
                case 1: snprintf(valBuf, sizeof(valBuf), "%d%%", curMaster); break;
                case 2: snprintf(valBuf, sizeof(valBuf), "%d%%", curBr); break;
                case 3: snprintf(valBuf, sizeof(valBuf), "+%ddB", curBoost); break;
                case 4:
                    if (curSlide == 0) snprintf(valBuf, sizeof(valBuf), "Off");
                    else snprintf(valBuf, sizeof(valBuf), "%ds", curSlide);
                    break;
                case 5: snprintf(valBuf, sizeof(valBuf), "Exec"); valCol = POKO_CLR_WARN; break;
                case 6: snprintf(valBuf, sizeof(valBuf), "Restart"); valCol = POKO_CLR_ERR; break;
            }

            _canvas->setTextColor(valCol, isSel ? theme.surface : theme.bg);
            _canvas->getTextBounds(valBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(124 - w, rowY + 9);
            _canvas->print(valBuf);
        }

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, 114, 128, theme.line);
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
            case 4: { // Cycle Slide Timer: 0 -> 3 -> 5 -> 10 -> 15 -> 30 -> 60 -> 0
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
            case 5: { // Reset Drivers
                handleDriverReset();
                break;
            }
            case 6: { // Reboot
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
        _dirty = true;
    }

    void onRight() {
        _selected = (_selected + 1) % ITEM_COUNT;
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
        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
};
