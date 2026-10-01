#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"
#include "AudioManager.h"
#include "BatteryManager.h"

extern BatteryManager batteryManager;

// ─────────────────────────────────────────────────────────────
//  PokoUI — Launcher carousel (7 tiles) + status bar (128×128)
// ─────────────────────────────────────────────────────────────

uint16_t getTileAccentColor(AppState state);

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

    static constexpr uint8_t TILE_COUNT = 8;
    PokoTile _tiles[TILE_COUNT] = {
        { "Info",     "System Info",     0x07E0, STATE_INFO,         "i"  },
        { "Clock",    "IST Clock",       0x07FF, STATE_CLOCK,        "12" },
        { "SSync",    "Snapclient",      0x07E0, STATE_SSYNC,        "S"  },
        { "Music",    "Audio Player",    0xF81F, STATE_MUSIC_UI,     "~"  },
        { "Video",    "Video Stream",    0x001F, STATE_VIDEO_UI,     ">"  },
        { "Gallery",  "Photo Viewer",    0xFD20, STATE_GALLERY_UI,   "#"  },
        { "Pixels",   "NeoPixel Ring",   0xFBE0, STATE_PIXELS_UI,    "*"  },
        { "Settings", "Preferences",     0x8410, STATE_SETTINGS_UI,  "*"  }
    };

    void drawStatusBar();

    void drawBatteryIcon(int x, int y, int pct, bool charging);

    // Draw a per-app icon centred at (cx,cy) using GFX primitives
    void drawAppIcon(int cx, int cy, AppState state, uint16_t color, uint16_t bg);

    void drawTile(uint8_t idx);

    void drawNavIndicator();

public:
    PokoUI(Arduino_GFX* gfx, AppSwitchFn switchFn);

    void begin();

    void redraw();

    void updateStatusBar();

    void renderDirect();

    void setTileSubtitle(AppState s, const char* sub);

    void flashHighlight();

    void navigateLeft();

    void navigateRight();

    void enter();

    uint8_t selectedIndex() const;
    AppState selectedState() const;

    void update();
};

