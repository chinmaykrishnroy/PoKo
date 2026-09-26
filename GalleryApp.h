#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "PokoAppState.h"
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  GalleryApp — Photo viewer (128×128)
//  Browses photos stored in LittleFS / backend server with
//  double-buffered rendering.
// ─────────────────────────────────────────────────────────────

class GalleryApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _photoIdx  = 0;
    static constexpr uint8_t TOTAL_PHOTOS = 4;

    void renderToCanvas() {
        if (!_canvas) return;

        _canvas->fillScreen(POKO_CLR_BG);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, 0x0841);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(0xFD20, 0x0841);
        _canvas->setCursor(3, 11);
        _canvas->print("Gallery");

        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _photoIdx + 1, TOTAL_PHOTOS);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 11);
        _canvas->print(countBuf);

        // Photo display frame (y=18..98)
        _canvas->drawRect(14, 18, 100, 80, 0x18C3);

        // Render photo pattern or LittleFS image
        switch (_photoIdx) {
            case 0: // Sunset Gradient
                for (int y = 20; y < 96; y++) {
                    uint16_t col = ((31 * (y - 20) / 76) << 11) | ((30 * (96 - y) / 76) << 5);
                    _canvas->drawFastHLine(16, y, 96, col);
                }
                _canvas->fillCircle(64, 58, 14, 0xFFE0);
                break;
            case 1: // Cyber Grid
                _canvas->fillRect(16, 20, 96, 76, 0x0821);
                for (int x = 20; x < 110; x += 12) _canvas->drawFastVLine(x, 20, 76, 0x07FF);
                for (int y = 24; y < 94; y += 12)  _canvas->drawFastHLine(16, y, 96, 0x07FF);
                break;
            case 2: // Mountain Scene
                _canvas->fillRect(16, 20, 96, 76, 0x001F);
                _canvas->fillTriangle(16, 95, 54, 38, 92, 95, 0x4208);
                _canvas->fillTriangle(50, 95, 84, 46, 111, 95, 0x632C);
                _canvas->fillCircle(100, 32, 8, 0xFFE0);
                break;
            case 3: // Poko Mascot Pattern
                _canvas->fillRect(16, 20, 96, 76, 0x1082);
                _canvas->drawRoundRect(36, 32, 56, 50, 10, POKO_CLR_ACCENT);
                _canvas->fillCircle(52, 50, 4, 0xFFFF);
                _canvas->fillCircle(76, 50, 4, 0xFFFF);
                _canvas->drawArc(64, 62, 10, 8, 0, 180, 0xFD20);
                break;
        }

        // Subtitle (y=104)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        const char* titles[] = { "Sunset Bloom", "Neon Horizon", "Alps Peak", "Poko Spirit" };
        _canvas->getTextBounds(titles[_photoIdx], 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 108);
        _canvas->print(titles[_photoIdx]);

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, 0x0841);
        _canvas->drawFastHLine(0, 114, 128, 0x18C3);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        const char* hint = "Boot:Prev  Key:Next  D-Boot:X";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    GalleryApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active   = true;
        _photoIdx = 0;
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
        _photoIdx = (_photoIdx == 0) ? (TOTAL_PHOTOS - 1) : (_photoIdx - 1);
        _dirty = true;
    }

    void onRight() {
        _photoIdx = (_photoIdx + 1) % TOTAL_PHOTOS;
        _dirty = true;
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        onRight();
    }

    void update() {
        if (!_active) return;
        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
};
