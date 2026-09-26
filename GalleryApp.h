#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"

// ─────────────────────────────────────────────────────────────
//  GalleryApp — Photo viewer (128×128)
//  Shows controls for 2s of inactivity then transitions to
//  immersive fullscreen (128×128 image only).
//  Single press: Next image (Nxt).
//  Double press: Exits fullscreen / Exits to launcher.
//  Configurable slideshow auto-timer supported via Preferences.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class GalleryApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active          = false;
    bool     _dirty           = true;
    bool     _fullscreen      = false;
    uint8_t  _photoIdx        = 0;
    uint32_t _lastActivityMs  = 0;
    uint32_t _lastSlideMs     = 0;

    static constexpr uint8_t TOTAL_PHOTOS = 4;

    void drawPhotoGraphic(int x, int y, int w, int h, uint8_t idx) {
        if (!_canvas) return;

        switch (idx) {
            case 0: { // Sunset Horizon
                for (int py = 0; py < h; py++) {
                    uint16_t r = (31 * py) / h;
                    uint16_t g = (45 * (h - py)) / h;
                    uint16_t b = (10 * (h - py)) / h;
                    uint16_t col = (r << 11) | (g << 5) | b;
                    _canvas->drawFastHLine(x, y + py, w, col);
                }
                int sunR = min(w, h) / 5;
                _canvas->fillCircle(x + w / 2, y + (h * 4) / 7, sunR, 0xFFE0);
                // Water reflection line
                for (int py = (h * 4) / 7 + sunR; py < h; py += 3) {
                    _canvas->drawFastHLine(x + w / 4, y + py, w / 2, 0xFBE0);
                }
                break;
            }
            case 1: { // Cyber Neon Grid
                _canvas->fillRect(x, y, w, h, 0x0821);
                int xStep = max(8, w / 8);
                int yStep = max(8, h / 8);
                for (int px = x; px <= x + w; px += xStep) {
                    _canvas->drawFastVLine(px, y, h, 0x07FF);
                }
                for (int py = y; py <= y + h; py += yStep) {
                    _canvas->drawFastHLine(x, py, w, 0x07FF);
                }
                // Center neon polygon
                _canvas->drawRoundRect(x + w / 4, y + h / 4, w / 2, h / 2, 6, 0xF81F);
                break;
            }
            case 2: { // Mountain Scene
                _canvas->fillRect(x, y, w, h, 0x0012); // Deep night sky
                // Stars
                _canvas->drawPixel(x + w / 5, y + h / 6, 0xFFFF);
                _canvas->drawPixel(x + (w * 3) / 4, y + h / 5, 0xFFFF);
                _canvas->drawPixel(x + w / 2, y + h / 8, 0xFFE0);
                // Moon
                _canvas->fillCircle(x + (w * 4) / 5, y + h / 4, max(4, w / 12), 0xFFE0);
                // Peaks
                _canvas->fillTriangle(x, y + h - 1, x + w / 3, y + h / 3, x + (w * 2) / 3, y + h - 1, 0x4208);
                _canvas->fillTriangle(x + w / 3, y + h - 1, x + (w * 2) / 3, y + (h * 2) / 5, x + w - 1, y + h - 1, 0x632C);
                break;
            }
            case 3: { // PoKo Emblem Mascot
                _canvas->fillRect(x, y, w, h, 0x1082);
                int mw = (w * 6) / 10;
                int mh = (h * 6) / 10;
                int mx = x + (w - mw) / 2;
                int my = y + (h - mh) / 2;
                _canvas->drawRoundRect(mx, my, mw, mh, 10, POKO_CLR_ACCENT);
                _canvas->fillRoundRect(mx + 2, my + 2, mw - 4, mh - 4, 8, 0x18C3);
                // Eyes
                int eyeR = max(2, mw / 12);
                _canvas->fillCircle(mx + mw / 3, my + mh / 3, eyeR, 0xFFFF);
                _canvas->fillCircle(mx + (mw * 2) / 3, my + mh / 3, eyeR, 0xFFFF);
                // Smile
                _canvas->drawArc(mx + mw / 2, my + (mh * 6) / 10, mw / 5, mw / 6, 0, 180, 0xFD20);
                break;
            }
        }
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        if (_fullscreen) {
            // Fullscreen 128×128 image only — zero borders, zero UI
            drawPhotoGraphic(0, 0, 128, 128, _photoIdx);
        } else {
            // Normal Windowed mode with navigation hints
            _canvas->fillScreen(theme.bg);

            // Header (y=0..13)
            _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(0xFD20, theme.headerBg);
            _canvas->setCursor(3, 11);
            _canvas->print("Gallery");

            char countBuf[8];
            snprintf(countBuf, sizeof(countBuf), "%d/%d", _photoIdx + 1, TOTAL_PHOTOS);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.headerBg);
            int16_t x1, y1; uint16_t w, h;
            _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(125 - w, 11);
            _canvas->print(countBuf);

            // Photo display frame (y=18..98)
            _canvas->drawRect(14, 18, 100, 80, theme.line);
            drawPhotoGraphic(16, 20, 96, 76, _photoIdx);

            // Subtitle (y=104)
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            const char* titles[] = { "Sunset Bloom", "Neon Horizon", "Alps Peak", "PoKo Mascot" };
            _canvas->getTextBounds(titles[_photoIdx], 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 108);
            _canvas->print(titles[_photoIdx]);

            // Footer (y=114..127)
            _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
            _canvas->drawFastHLine(0, 114, 128, theme.line);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.footerText, theme.headerBg);
            const char* hint = "L:Prv  R:Nxt  2R:Back";
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);
        }

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
        _active          = true;
        _dirty           = true;
        _fullscreen      = false;
        _photoIdx        = 0;
        _lastActivityMs  = millis();
        _lastSlideMs     = millis();
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
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        if (_fullscreen) {
            // Single press in fullscreen advances to Nxt photo
            _photoIdx = (_photoIdx + 1) % TOTAL_PHOTOS;
        } else {
            // In windowed mode, BOOT goes to Previous photo
            _photoIdx = (_photoIdx == 0) ? (TOTAL_PHOTOS - 1) : (_photoIdx - 1);
        }
        _dirty = true;
    }

    void onRight() {
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        // Advance to Next photo
        _photoIdx = (_photoIdx + 1) % TOTAL_PHOTOS;
        _dirty = true;
    }

    void onBack() {
        if (_fullscreen) {
            // Double press in fullscreen exits fullscreen back to windowed mode
            _fullscreen = false;
            _lastActivityMs = millis();
            _dirty = true;
        } else {
            // In windowed mode, exit to launcher
            if (_exit) _exit(STATE_LAUNCHER);
        }
    }

    void onEnter() {
        onBack();
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();

        // 2-Second Inactivity Fullscreen Transition
        if (!_fullscreen && (now - _lastActivityMs >= 2000)) {
            _fullscreen = true;
            _dirty = true;
        }

        // Slideshow Auto-advance Timer
        int slideInterval = prefs.getInt("gallery_timer", 0);
        if (slideInterval > 0 && (now - _lastSlideMs >= (uint32_t)slideInterval * 1000)) {
            _lastSlideMs = now;
            _photoIdx = (_photoIdx + 1) % TOTAL_PHOTOS;
            _dirty = true;
        }

        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
};
