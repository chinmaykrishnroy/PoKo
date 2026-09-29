#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoUI.h"

#include "PokoTheme.h"
#include "AudioManager.h"
#include "PowerManager.h"

extern PowerManager* powerManager;

// ─────────────────────────────────────────────────────────────
//  ClockApp — Digital Clock (128×128) with NTP sync (IST)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

class ClockApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    bool     _is24h     = true;
    uint8_t  _style     = 0;  // 0: Modern, 1: Minimal
    uint32_t _lastDrawMs = 0;

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        _canvas->fillScreen(theme.bg);

        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        bool timeValid = (timeinfo.tm_year > (2020 - 1900));

        if (_style == 0) {
            // ── Modern Style ──────────────────────────────────────────

            // Top Status Bar (y=0..14)
            _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
            _canvas->setFont(u8g2_font_profont10_mf);
            _canvas->setTextColor(theme.muted, theme.headerBg);
            _canvas->setCursor(3, 10);
            _canvas->print("Clock");

            if (audioManager) {
                audioManager->drawStatusDot(_canvas, 36, 6, 2);
            }

            _canvas->setFont(u8g2_font_profont10_mf);
            _canvas->setTextColor(theme.muted, theme.headerBg);
            _canvas->setCursor(85, 10);
            _canvas->print("GMT+5:30");

            // Date / Day (y=16..32)
            char dateBuf[20];
            if (timeValid) {
                strftime(dateBuf, sizeof(dateBuf), "%a, %d %b", &timeinfo);
            } else {
                snprintf(dateBuf, sizeof(dateBuf), "Awaiting NTP...");
            }
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.accent, theme.bg);
            int16_t x1, y1; uint16_t w, h;
            _canvas->getTextBounds(dateBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 34);
            _canvas->print(dateBuf);

            // Big Clock Digits (y=38..78)
            char timeBuf[12];
            char secBuf[6];
            if (timeValid) {
                if (_is24h) {
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
                } else {
                    strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
                }
                strftime(secBuf, sizeof(secBuf), ":%S", &timeinfo);
            } else {
                uint32_t up = millis() / 1000;
                snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", (unsigned long)(up / 60), (unsigned long)(up % 60));
                snprintf(secBuf, sizeof(secBuf), "");
            }

            _canvas->setFont(u8g2_font_logisoso24_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            _canvas->getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(56 - w / 2, 74);
            _canvas->print(timeBuf);

            // Seconds text — positioned 4 px right of where HH:MM ends
            int16_t timeEndX = (56 - w / 2) + (int16_t)w + 4;
            _canvas->setFont(u8g2_font_helvB10_tf);
            _canvas->setTextColor(theme.accent, theme.bg);
            _canvas->setCursor(timeEndX, 70);
            _canvas->print(secBuf);

            // Seconds progress bar (y=84..88)
            int secVal = timeValid ? timeinfo.tm_sec : ((millis() / 1000) % 60);
            _canvas->drawRect(14, 84, 100, 5, theme.line);
            int barW = (96 * secVal) / 59;
            _canvas->fillRect(16, 85, barW, 3, theme.accent);

            // Subtitle info
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            const char* modeStr = _is24h ? "24-HOUR FORMAT" : "12-HOUR FORMAT";
            _canvas->getTextBounds(modeStr, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 102);
            _canvas->print(modeStr);

        } else {
            // ── Minimalist Style ─────────────────────────────────────
            char timeBuf[12];
            if (timeValid) {
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
            } else {
                snprintf(timeBuf, sizeof(timeBuf), "--:--");
            }
            _canvas->setFont(u8g2_font_logisoso28_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            int16_t x1, y1; uint16_t w, h;
            _canvas->getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 72);
            _canvas->print(timeBuf);

            char dateBuf[20];
            if (timeValid) {
                strftime(dateBuf, sizeof(dateBuf), "%d %B %Y", &timeinfo);
            } else {
                snprintf(dateBuf, sizeof(dateBuf), "IST Clock");
            }
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            _canvas->getTextBounds(dateBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 94);
            _canvas->print(dateBuf);
        }

        // Footer Navigation Bar (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, 114, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = "L:Mode  R:Style  2R:Back";
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    ClockApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active = true;
        _dirty  = true;
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

    void refreshTheme() { if (_active) renderToCanvas(); }

    void onLeft() {
        _is24h = !_is24h;
        _dirty = true;
    }

    void onRight() {
        _style = (_style + 1) % 2;
        _dirty = true;
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        onBack();
    }

    void update() {
        if (!_active) return;
        if (powerManager && (powerManager->getDisplayState() == DISPLAY_POWER_OFF || powerManager->getDisplayState() == DISPLAY_POWER_SLEEP)) {
            return; // Don't redraw while screen is asleep
        }
        uint32_t now = millis();
        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        bool isDimmed = (powerManager && powerManager->getDisplayState() == DISPLAY_POWER_DIMMED);
        uint32_t interval = audioBlinking ? 100 : (isDimmed ? 60000 : 1000);
        if (now - _lastDrawMs >= interval) {
            _lastDrawMs = now;
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
};

