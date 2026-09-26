#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

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

    static constexpr uint8_t ITEM_COUNT = 6;
    const char* _items[ITEM_COUNT] = {
        "Brightness",
        "Volume",
        "LED Ring",
        "WiFi Status",
        "Reset Drivers",
        "Reboot Device"
    };

    void renderToCanvas() {
        if (!_canvas) return;

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 13, 0x0841);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(POKO_CLR_ACCENT, 0x0841);
        _canvas->setCursor(3, 10);
        _canvas->print("Settings");

        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _selected + 1, ITEM_COUNT);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 10);
        _canvas->print(countBuf);

        // Menu items area (y=14..113)
        _canvas->fillRect(0, 14, 128, 100, POKO_CLR_BG);
        _canvas->setFont(u8g2_font_profont10_mf);

        int curBr  = prefs.getInt("brightness", 80);
        int curVol = prefs.getInt("volume", 75);

        for (uint8_t i = 0; i < ITEM_COUNT; i++) {
            int16_t rowY = 16 + i * 16;
            bool isSel = (i == _selected);

            if (isSel) {
                _canvas->fillRect(0, rowY - 2, 128, 15, 0x18C3);
            }

            _canvas->setTextColor(isSel ? POKO_CLR_TEXT : POKO_CLR_DIM, isSel ? 0x18C3 : POKO_CLR_BG);
            _canvas->setCursor(4, rowY + 9);
            _canvas->print(_items[i]);

            // Value text on the right
            char valBuf[14] = "";
            uint16_t valCol = isSel ? POKO_CLR_ACCENT : POKO_CLR_DIM;

            switch (i) {
                case 0: snprintf(valBuf, sizeof(valBuf), "%d%%", curBr); break;
                case 1: snprintf(valBuf, sizeof(valBuf), "%d%%", curVol); break;
                case 2: snprintf(valBuf, sizeof(valBuf), "Active"); break;
                case 3: snprintf(valBuf, sizeof(valBuf), WiFi.status() == WL_CONNECTED ? "STA" : "AP"); break;
                case 4: snprintf(valBuf, sizeof(valBuf), "Exec"); valCol = POKO_CLR_WARN; break;
                case 5: snprintf(valBuf, sizeof(valBuf), "Restart"); valCol = POKO_CLR_ERR; break;
            }

            _canvas->setTextColor(valCol, isSel ? 0x18C3 : POKO_CLR_BG);
            _canvas->getTextBounds(valBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(124 - w, rowY + 9);
            _canvas->print(valBuf);
        }

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, 0x0841);
        _canvas->drawFastHLine(0, 114, 128, 0x18C3);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        const char* hint = "Boot:Up  Key:Dn  D-Key:Set";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

    void applyAction() {
        switch (_selected) {
            case 0: { // Cycle Brightness: 25% -> 50% -> 75% -> 100% -> 25%
                int b = prefs.getInt("brightness", 80);
                if (b <= 30)      b = 50;
                else if (b <= 60) b = 75;
                else if (b <= 85) b = 100;
                else              b = 25;
                prefs.putInt("brightness", b);
                setBacklightPercent(b);
                break;
            }
            case 1: { // Cycle Volume: 25% -> 50% -> 75% -> 100%
                int v = prefs.getInt("volume", 75);
                if (v <= 30)      v = 50;
                else if (v <= 60) v = 75;
                else if (v <= 85) v = 100;
                else              v = 25;
                prefs.putInt("volume", v);
                es8311SetVolume(v);
                break;
            }
            case 2: { // Cycle LED colors
                static uint8_t c = 0;
                c = (c + 1) % 5;
                if (c == 0) turnOffLEDs();
                else if (c == 1) setAllLEDs(CRGB(0, 180, 255)); // Cyan
                else if (c == 2) setAllLEDs(CRGB(0, 255, 0));   // Green
                else if (c == 3) setAllLEDs(CRGB(255, 180, 0)); // Amber
                else if (c == 4) setAllLEDs(CRGB(255, 0, 0));   // Red
                break;
            }
            case 3: { // No-op info
                break;
            }
            case 4: { // Reset Drivers
                handleDriverReset();
                break;
            }
            case 5: { // Reboot
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
