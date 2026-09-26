#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "PokoAppState.h"
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  PokoUI — Launcher carousel + status bar for 128×128 display
//
//  Layout:
//    y=0..12    Status bar  (WiFi status, uptime)
//    y=13..114  Tile area   (rounded box, icon/accent, title, subtitle)
//    y=115..127 Nav dots / bottom hint
//
//  Direct drawing on Arduino_GFX using U8g2 fonts natively.
// ─────────────────────────────────────────────────────────────

// ── Color palette ─────────────────────────────────────────────
#define POKO_CLR_BG        0x0000  // black
#define POKO_CLR_ACCENT    0x07FF  // cyan
#define POKO_CLR_TEXT      0xFFFF  // white
#define POKO_CLR_DIM       0x8410  // mid-grey
#define POKO_CLR_WARN      0xFD20  // orange
#define POKO_CLR_ERR       0xF800  // red
#define POKO_CLR_GREEN     0x07E0  // green
#define POKO_CLR_TILE_BG   0x1082  // dark tile background

struct PokoTile {
    const char* name;
    const char* subtitle;
    uint16_t    accentColor;
    AppState    state;
};

extern String getNetworkStatusMsg();

class PokoUI {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _switchApp;
    Arduino_Canvas* _statusCanvas = nullptr;

    uint8_t       _selected = 0;
    bool          _dirty    = true;
    uint32_t      _lastStatusMs = 0;

    // Single app tile for now per user instruction:
    // "I want to create just one app tile for now... and I want info app"
    static constexpr uint8_t TILE_COUNT = 1;
    PokoTile _tiles[TILE_COUNT] = {
        { "Info", "System Info", POKO_CLR_GREEN, STATE_INFO }
    };

    void drawStatusBar() {
        if (!_statusCanvas) return;
        _statusCanvas->fillScreen(0x0841);
        _statusCanvas->setFont(u8g2_font_profont10_mf);
        _statusCanvas->setTextSize(1);

        String net = getNetworkStatusMsg();
        if (net.length() == 0) {
            _statusCanvas->setTextColor(POKO_CLR_GREEN, 0x0841);
            _statusCanvas->setCursor(2, 9);
            _statusCanvas->print("WiFi OK");
        } else {
            _statusCanvas->setTextColor(POKO_CLR_WARN, 0x0841);
            _statusCanvas->setCursor(2, 9);
            if (net.length() > 13) net = net.substring(0, 12) + "~";
            _statusCanvas->print(net);
        }

        char buf[12];
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > (2020 - 1900)) {
            strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        } else {
            uint32_t up = millis() / 1000;
            if (up < 3600) snprintf(buf, sizeof(buf), "%lus", (unsigned long)up);
            else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(up / 60));
        }

        _statusCanvas->setTextColor(POKO_CLR_DIM, 0x0841);
        int16_t x1, y1; uint16_t w, h;
        _statusCanvas->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        _statusCanvas->setCursor(126 - w, 9);
        _statusCanvas->print(buf);

        _statusCanvas->flush();
    }

    void drawTile(uint8_t idx) {
        const PokoTile& t = _tiles[idx];

        // Clear main area
        _gfx->fillRect(0, 13, 128, 102, POKO_CLR_BG);

        // Accent header line
        _gfx->fillRect(0, 13, 128, 2, t.accentColor);

        // Rounded box for app icon / emblem
        _gfx->drawRoundRect(36, 22, 56, 44, 8, t.accentColor);
        _gfx->fillRoundRect(38, 24, 52, 40, 6, 0x0821);

        // Draw "i" emblem or tile graphic
        _gfx->setFont(u8g2_font_helvB14_tf);
        _gfx->setTextColor(t.accentColor, 0x0821);
        int16_t x1, y1; uint16_t w, h;
        _gfx->getTextBounds("i", 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 50);
        _gfx->print("i");

        // App Name
        _gfx->setFont(u8g2_font_helvB10_tf);
        _gfx->setTextColor(POKO_CLR_TEXT, POKO_CLR_BG);
        _gfx->getTextBounds(t.name, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 80);
        _gfx->print(t.name);

        // Subtitle
        if (t.subtitle && strlen(t.subtitle) > 0) {
            _gfx->setFont(u8g2_font_profont10_mf);
            _gfx->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
            _gfx->getTextBounds(t.subtitle, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(64 - w / 2, 95);
            _gfx->print(t.subtitle);
        }

        // Bottom instruction: ">> Click to open"
        _gfx->setFont(u8g2_font_5x7_tf);
        _gfx->setTextColor(POKO_CLR_DIM, POKO_CLR_BG);
        const char* hint = "Dbl-Key: Open";
        _gfx->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 110);
        _gfx->print(hint);
    }

    void drawNavIndicator() {
        _gfx->fillRect(0, 116, 128, 12, POKO_CLR_BG);
        // Draw 1 active pill in center
        _gfx->fillRoundRect(60, 120, 8, 4, 2, POKO_CLR_ACCENT);
    }

public:
    PokoUI(Arduino_GFX* gfx, AppSwitchFn switchFn)
        : _gfx(gfx), _switchApp(switchFn) {}

    void begin() {
        _statusCanvas = new Arduino_Canvas(128, 13, _gfx, 0, 0);
        _statusCanvas->begin();
        redraw();
    }

    void redraw() { _dirty = true; }

    void setTileSubtitle(AppState s, const char* sub) {
        int idx = stateToTile(s);
        if (idx < 0 || idx >= TILE_COUNT) return;
        _tiles[idx].subtitle = sub;
        if (_selected == (uint8_t)idx) _dirty = true;
    }

    void flashHighlight() {
        _gfx->drawRoundRect(34, 20, 60, 48, 10, WHITE);
        delay(40);
        _gfx->drawRoundRect(34, 20, 60, 48, 10, POKO_CLR_BG);
        _gfx->drawRoundRect(36, 22, 56, 44, 8, _tiles[_selected].accentColor);
    }

    void navigateLeft() {
        _selected = (_selected == 0) ? (TILE_COUNT - 1) : (_selected - 1);
        flashHighlight();
        _dirty = true;
    }

    void navigateRight() {
        _selected = (_selected + 1) % TILE_COUNT;
        flashHighlight();
        _dirty = true;
    }

    void enter() {
        if (_switchApp) _switchApp(_tiles[_selected].state);
    }

    uint8_t selectedIndex() const { return _selected; }
    AppState selectedState() const { return _tiles[_selected].state; }

    void update() {
        uint32_t now = millis();
        if (now - _lastStatusMs >= 1000) {
            _lastStatusMs = now;
            drawStatusBar();
        }
        if (_dirty) {
            _dirty = false;
            drawStatusBar();
            drawTile(_selected);
            drawNavIndicator();
        }
    }
};
