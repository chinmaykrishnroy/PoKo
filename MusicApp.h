#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

// ─────────────────────────────────────────────────────────────
//  MusicApp — Audio Streaming Player UI (128×128)
//  Shows track title, artist, audio visualizer, and playback state.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class MusicApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    bool     _playing   = false;
    int      _volume    = 75;
    uint32_t _trackPos  = 0;
    uint32_t _trackLen  = 210; // seconds
    uint32_t _lastDrawMs = 0;

    String   _title  = "Audio Stream";
    String   _artist = "Poko Player";

    void renderToCanvas() {
        if (!_canvas) return;

        _canvas->fillScreen(POKO_CLR_BG);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, 0x0841);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(0xF81F, 0x0841);
        _canvas->setCursor(3, 11);
        _canvas->print("Music");

        // Status text
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(_playing ? POKO_CLR_GREEN : POKO_CLR_DIM, 0x0841);
        const char* st = _playing ? "PLAYING" : "PAUSED";
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(st, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 11);
        _canvas->print(st);

        // Album Art / Graphic Placeholder (y=18..52)
        _canvas->drawRoundRect(46, 18, 36, 34, 6, 0xF81F);
        _canvas->fillRoundRect(48, 20, 32, 30, 4, 0x0821);
        _canvas->setFont(u8g2_font_helvB14_tf);
        _canvas->setTextColor(0xF81F, 0x0821);
        _canvas->setCursor(58, 42);
        _canvas->print(_playing ? ">" : "||");

        // Track Title (y=62)
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(POKO_CLR_TEXT, POKO_CLR_BG);
        _canvas->getTextBounds(_title, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 64);
        _canvas->print(_title);

        // Artist (y=74)
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->getTextBounds(_artist, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 76);
        _canvas->print(_artist);

        // Animated Spectrum Visualizer (y=80..92)
        for (int i = 0; i < 10; i++) {
            int bh = _playing ? random(3, 12) : 2;
            _canvas->fillRect(20 + i * 9, 92 - bh, 6, bh, 0xF81F);
        }

        // Progress Bar (y=98..104)
        _canvas->drawRect(14, 98, 100, 5, 0x18C3);
        int progW = (_trackLen > 0) ? (96 * (_trackPos % _trackLen)) / _trackLen : 0;
        _canvas->fillRect(16, 99, progW, 3, 0xF81F);

        // Track time
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu / %02lu:%02lu",
                 (unsigned long)(_trackPos / 60), (unsigned long)(_trackPos % 60),
                 (unsigned long)(_trackLen / 60), (unsigned long)(_trackLen % 60));
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 110);
        _canvas->print(timeStr);

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, 0x0841);
        _canvas->drawFastHLine(0, 114, 128, 0x18C3);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        const char* hint = "D-Key:Play/Pause  D-Boot:X";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    MusicApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        _volume = prefs.getInt("volume", 75);
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

    void onLeft() {
        if (_volume > 0) {
            _volume = max(0, _volume - 5);
            prefs.putInt("volume", _volume);
            es8311SetVolume(_volume);
            _dirty = true;
        }
    }

    void onRight() {
        if (_volume < 100) {
            _volume = min(100, _volume + 5);
            prefs.putInt("volume", _volume);
            es8311SetVolume(_volume);
            _dirty = true;
        }
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        _playing = !_playing;
        _dirty = true;
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        if (_playing && (now - _lastDrawMs >= 500)) {
            _lastDrawMs = now;
            _trackPos = (_trackPos + 1) % _trackLen;
            _dirty = true;
        } else if (!_playing && (now - _lastDrawMs >= 1000)) {
            _lastDrawMs = now;
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
};
