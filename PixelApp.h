#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"
#include "PixelEngine.h"
#include "AudioManager.h"

// ─────────────────────────────────────────────────────────────
//  PixelApp — On-device NeoPixel Ring Studio (128×128)
//  Features:
//    - Real-time Color Swatch & 8-LED ring preview diagram
//    - R, G, B color sliders with live feedback
//    - Target pixel selection (All 8 or individual LED 1..8)
//    - Animation preset selection (Spinner, Rainbow, Breathe, etc.)
//    - Music & SSync reactive lighting controls
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern AudioManager* audioManager;

class PixelApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _selected  = 0;
    uint8_t  _scroll    = 0;
    uint32_t _lastDrawMs = 0;
    uint32_t _lastEditMs = 0;
    bool _savePending = false;

    static constexpr uint8_t ITEM_COUNT   = 11;
    static constexpr uint8_t ROW_H        = 14;
    static constexpr uint8_t TOP_Y        = 42;
    static constexpr uint8_t ROWS_VISIBLE = 5;
    static constexpr uint8_t FOOTER_Y     = 114;

    const char* _items[ITEM_COUNT] = {
        "Mode",
        "Red",
        "Green",
        "Blue",
        "Target",
        "Bright",
        "Music Light",
        "Music FX",
        "SSync Light",
        "SSync FX",
        "Freq Resp"
    };

    void adjustScroll();

    uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b);

public:
    void renderToCanvas();

    void applyAction();

    void adjustCurrentValue(int delta);

public:
    PixelApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    void begin();

    void load();

    void unload();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void onHoldingLeft();

    void onHoldingRight();

    void onBack();

    void onEnter();

    void update();
};

