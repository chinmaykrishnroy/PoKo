#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

// ─────────────────────────────────────────────────────────────
//  SSyncApp — Direct Snapcast Client UI (128×128)
//  Shows connection status, volume, server, and audio stream info.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class SSyncApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    bool     _connected = false;
    bool     _playing   = false;
    bool     _muted     = false;
    int      _volume    = 75;
    uint32_t _lastDrawMs = 0;

    String   _serverHost = "192.168.1.100";
    uint16_t _serverPort = 1704;
    String   _codec      = "FLAC 48k";

    void renderToCanvas() {
        if (!_canvas) return;

        _canvas->fillScreen(POKO_CLR_BG);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, 0x0841);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(POKO_CLR_ACCENT, 0x0841);
        _canvas->setCursor(3, 11);
        _canvas->print("SSync");

        // Status badge right side
        uint16_t badgeCol = _connected ? (_playing ? POKO_CLR_GREEN : POKO_CLR_ACCENT) : POKO_CLR_ERR;
        const char* badgeText = _connected ? (_playing ? "PLAYING" : "IDLE") : "OFFLINE";
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(badgeCol, 0x0841);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(badgeText, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 11);
        _canvas->print(badgeText);

        // Status Card (y=18..44)
        _canvas->drawRoundRect(6, 18, 116, 26, 4, 0x18C3);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->setCursor(12, 30);
        _canvas->print("Server:");
        _canvas->setTextColor(POKO_CLR_TEXT, POKO_CLR_BG);
        _canvas->setCursor(54, 30);
        String srv = _serverHost.length() > 11 ? _serverHost.substring(0, 10) + ".." : _serverHost;
        _canvas->print(srv);

        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->setCursor(12, 40);
        _canvas->print("Codec:");
        _canvas->setTextColor(POKO_CLR_ACCENT, POKO_CLR_BG);
        _canvas->setCursor(54, 40);
        _canvas->print(_codec);

        // Sync & Buffer Info (y=48..68)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        _canvas->setCursor(12, 58);
        _canvas->print("LATENCY: 0 ms");
        _canvas->setCursor(12, 68);
        _canvas->print("BUFFER: 1000 ms");

        // Volume Bar (y=76..102)
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(POKO_CLR_TEXT, POKO_CLR_BG);
        _canvas->setCursor(12, 84);
        char volBuf[16];
        snprintf(volBuf, sizeof(volBuf), "Volume: %d%%%s", _volume, _muted ? " (MUTED)" : "");
        _canvas->print(volBuf);

        // Progress bar frame
        _canvas->drawRect(12, 90, 104, 10, RGB565_WHITE);
        int filled = (100 * _volume) / 100;
        if (filled > 0) {
            _canvas->fillRect(14, 92, filled, 6, _muted ? POKO_CLR_DIM : POKO_CLR_GREEN);
        }

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, 0x0841);
        _canvas->drawFastHLine(0, 114, 128, 0x18C3);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(POKO_CLR_DIM, 0x0841);
        const char* hint = "Boot:Vol- Key:Vol+ D-Boot:X";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    SSyncApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        _serverHost = prefs.getString("snap_host", "192.168.1.100");
        _serverPort = prefs.getInt("snap_port", 1704);
        _volume     = prefs.getInt("volume", 75);
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
        _muted = !_muted;
        es8311Mute(_muted);
        _dirty = true;
    }

    void onLongRight() {
        _muted = !_muted;
        es8311Mute(_muted);
        _dirty = true;
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        if (now - _lastDrawMs >= 1000) {
            _lastDrawMs = now;
            _connected = (WiFi.status() == WL_CONNECTED);
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }

    // REST API status
    String apiJson() {
        String j = "{";
        j += "\"connected\":" + String(_connected ? "true" : "false") + ",";
        j += "\"playing\":" + String(_playing ? "true" : "false") + ",";
        j += "\"volume\":" + String(_volume) + ",";
        j += "\"server\":\"" + _serverHost + ":" + String(_serverPort) + "\",";
        j += "\"codec\":\"" + _codec + "\"";
        j += "}";
        return j;
    }
};
