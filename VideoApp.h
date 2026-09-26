#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  VideoApp — MJPEG / Video Stream UI (128×128)
//  Double-buffered rendering with stream diagnostics and playback.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class VideoApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    bool     _streaming = false;
    uint8_t  _fps       = 0;
    uint32_t _frames    = 0;
    uint32_t _lastDrawMs = 0;

    void renderToCanvas() {
        if (!_canvas) return;

        _canvas->fillScreen(POKO_CLR_BG);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, 0x0841);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(0x001F, 0x0841);
        _canvas->setCursor(3, 11);
        _canvas->print("Video");

        // Status badge
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(_streaming ? POKO_CLR_GREEN : POKO_CLR_DIM, 0x0841);
        const char* st = _streaming ? "STREAMING" : "STANDBY";
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(st, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 11);
        _canvas->print(st);

        // Viewport Frame (y=18..88)
        _canvas->drawRect(14, 18, 100, 70, 0x18C3);
        _canvas->fillRect(16, 20, 96, 66, 0x0821);

        if (_streaming) {
            // Draw animated test pattern / frame representation
            for (int y = 20; y < 86; y += 10) {
                _canvas->drawFastHLine(16, y, 96, 0x18C3);
            }
            char frameStr[16];
            snprintf(frameStr, sizeof(frameStr), "%d FPS", _fps);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(POKO_CLR_GREEN, 0x0821);
            _canvas->getTextBounds(frameStr, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 58);
            _canvas->print(frameStr);
        } else {
            // Play icon
            _canvas->setFont(u8g2_font_helvB14_tf);
            _canvas->setTextColor(0x001F, 0x0821);
            _canvas->setCursor(58, 58);
            _canvas->print(">");
        }

        // Stats (y=92..108)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->setCursor(16, 100);
        _canvas->print("RES: 128x128 TCP");
        _canvas->setCursor(16, 108);
        char buf[20];
        snprintf(buf, sizeof(buf), "PORT: %d", prefs.getInt("tcp_port", 1234));
        _canvas->print(buf);

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, 0x0841);
        _canvas->drawFastHLine(0, 114, 128, 0x18C3);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        const char* hint = "D-Key:Stream  D-Boot:X";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    VideoApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active  = true;
        _dirty   = true;
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

    void onLeft() {}
    void onRight() {}

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        _streaming = !_streaming;
        _fps = _streaming ? 24 : 0;
        _dirty = true;
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        if (_streaming && (now - _lastDrawMs >= 1000)) {
            _lastDrawMs = now;
            _frames += _fps;
            _dirty = true;
        } else if (!_streaming && (now - _lastDrawMs >= 1000)) {
            _lastDrawMs = now;
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
};
