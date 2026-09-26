#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"

// ─────────────────────────────────────────────────────────────
//  PokoUI — Launcher carousel (7 tiles) + status bar (128×128)
// ─────────────────────────────────────────────────────────────

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
    const char* emblem;
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

    static constexpr uint8_t TILE_COUNT = 7;
    PokoTile _tiles[TILE_COUNT] = {
        { "Info",     "System Info",     0x07E0, STATE_INFO,         "i"  },
        { "Clock",    "IST Clock",       0x07FF, STATE_CLOCK,        "12" },
        { "SSync",    "Snapclient",      0x07E0, STATE_SSYNC,        "S"  },
        { "Music",    "Audio Player",    0xF81F, STATE_MUSIC_UI,     "~"  },
        { "Video",    "Video Stream",    0x001F, STATE_VIDEO_UI,     ">"  },
        { "Gallery",  "Photo Viewer",    0xFD20, STATE_GALLERY_UI,   "#"  },
        { "Settings", "Preferences",     0x8410, STATE_SETTINGS_UI,  "*"  }
    };

    void drawStatusBar() {
        if (!_statusCanvas) return;
        const auto& theme = currentTheme();
        _statusCanvas->fillScreen(theme.headerBg);

        // Left: WiFi status dot
        String net = getNetworkStatusMsg();
        if (net.length() == 0) {
            _statusCanvas->fillCircle(7, 6, 2, 0x07E0); // Green
        } else {
            _statusCanvas->fillCircle(7, 6, 2, 0xFD20); // Amber
        }

        // Center: "PoKo" branding
        _statusCanvas->setFont(u8g2_font_helvB08_tf);
        _statusCanvas->setTextColor(theme.headerText, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _statusCanvas->getTextBounds("PoKo", 0, 0, &x1, &y1, &w, &h);
        _statusCanvas->setCursor(64 - w / 2, 10);
        _statusCanvas->print("PoKo");

        // Right: Live time
        char buf[12];
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > (2020 - 1900)) {
            strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        } else {
            uint32_t up = millis() / 1000;
            if (up < 3600) snprintf(buf, sizeof(buf), "%lus", (unsigned long)up);
            else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(up / 60));
        }

        _statusCanvas->setFont(u8g2_font_profont10_mf);
        _statusCanvas->setTextColor(theme.muted, theme.headerBg);
        _statusCanvas->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        _statusCanvas->setCursor(126 - w, 9);
        _statusCanvas->print(buf);

        _statusCanvas->flush();
    }

    void drawTile(uint8_t idx) {
        const PokoTile& t = _tiles[idx];
        const auto& theme = currentTheme();

        // Clear main carousel area with theme background
        _gfx->fillRect(0, 13, 128, 101, theme.bg);

        // Center carousel chevrons alongside the tile box
        _gfx->setFont(u8g2_font_helvB10_tf);
        _gfx->setTextColor(theme.line, theme.bg);
        _gfx->setCursor(4, 52);
        _gfx->print("<");
        _gfx->setCursor(118, 52);
        _gfx->print(">");

        // Rounded box for app emblem, centered lower from header
        _gfx->drawRoundRect(36, 26, 56, 42, 8, t.accentColor);
        _gfx->fillRoundRect(38, 28, 52, 38, 6, theme.surface);

        // Emblem symbol
        _gfx->setFont(u8g2_font_helvB14_tf);
        _gfx->setTextColor(t.accentColor, theme.surface);
        int16_t x1, y1; uint16_t w, h;
        _gfx->getTextBounds(t.emblem, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 54);
        _gfx->print(t.emblem);

        // App Name
        _gfx->setFont(u8g2_font_helvB10_tf);
        _gfx->setTextColor(theme.text, theme.bg);
        _gfx->getTextBounds(t.name, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 82);
        _gfx->print(t.name);

        // Subtitle
        if (t.subtitle && strlen(t.subtitle) > 0) {
            _gfx->setFont(u8g2_font_profont10_mf);
            _gfx->setTextColor(theme.muted, theme.bg);
            _gfx->getTextBounds(t.subtitle, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(64 - w / 2, 95);
            _gfx->print(t.subtitle);
        }

        // Footer Navigation Bar (y=114..127)
        _gfx->fillRect(0, 114, 128, 14, theme.headerBg);
        _gfx->drawFastHLine(0, 114, 128, theme.line);
        _gfx->setFont(u8g2_font_5x7_tf);
        _gfx->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = "L:Prv  R:Nxt  2R:Open";
        _gfx->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 124);
        _gfx->print(hint);
    }

    void drawNavIndicator() {
        const auto& theme = currentTheme();
        _gfx->fillRect(0, 102, 128, 11, theme.bg);
        int totalW = TILE_COUNT * 8 + (TILE_COUNT - 1) * 4;
        int startX = (128 - totalW) / 2;
        for (uint8_t i = 0; i < TILE_COUNT; i++) {
            int x = startX + i * 12;
            if (i == _selected) {
                _gfx->fillRoundRect(x, 105, 8, 4, 2, theme.accent);
            } else {
                _gfx->fillCircle(x + 4, 107, 2, theme.line);
            }
        }
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
        _gfx->drawRoundRect(34, 24, 60, 46, 10, RGB565_WHITE);
        delay(40);
        _gfx->drawRoundRect(34, 24, 60, 46, 10, currentTheme().bg);
        _gfx->drawRoundRect(36, 26, 56, 42, 8, _tiles[_selected].accentColor);
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
