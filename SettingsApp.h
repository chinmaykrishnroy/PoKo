#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"

#include "PokoTheme.h"
#include "AudioManager.h"
#include "SnapPlayer.h"
#include "PowerManager.h"

// ─────────────────────────────────────────────────────────────
//  SettingsApp — On-device settings menu (128×128)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern bool handleDriverReset();
extern SnapPlayer* snapService;
extern PowerManager* powerManager;

class SettingsApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _selected  = 0;
    uint8_t  _scroll    = 0;
    uint32_t _lastBlinkMs = 0;
    uint32_t _holdStartMs = 0;
    uint32_t _lastHoldMs = 0;
    int8_t   _holdDirection = 0;
    int8_t   _pendingItem = -1;

    static constexpr uint8_t ITEM_COUNT   = 17;
    static constexpr uint8_t ROW_H        = 16;
    static constexpr uint8_t TOP_Y        = 14;
    static constexpr uint8_t ROWS_VISIBLE = 6;
    static constexpr uint8_t FOOTER_Y     = 114;

    const char* _items[ITEM_COUNT] = {
        "Theme",
        "Master Vol",
        "Brightness",
        "Amp Boost",
        "Dim Timeout",
        "Sleep Timeout",
        "Auto-Off",
        "Off Battery %",
        "Ambient Clock",
        "WiFi Sleep",
        "USB Mode",
        "Slide Timer",
        "SSync Auto",
        "LED Bright",
        "Reset Drivers",
        "Power Off",
        "Reboot"
    };

    void adjustScroll();

    void flushHeldValue();

    void adjustHeldValue(int direction);

public:
    void renderToCanvas();

    void applyAction();

public:
    SettingsApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    void begin();

    void load();

    void unload();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void onBack();

    void onEnter();

    void onHoldingLeft();
    void onHoldingRight();

    void update();
};

