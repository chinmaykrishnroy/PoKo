#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoUI.h"

#include "PokoTheme.h"
#include "AudioManager.h"
#include "PowerManager.h"

extern PowerManager* powerManager;

// ─────────────────────────────────────────────────────────────
//  ClockApp — Digital Clock (128×128) with NTP sync (IST)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

class ClockApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    bool     _is24h     = true;
    uint8_t  _style     = 0;  // 0: Modern, 1: Minimal
    uint32_t _lastDrawMs = 0;

    void renderToCanvas();

public:
    ClockApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    void begin();

    void load();

    void unload();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void onBack();

    void onEnter();

    void update();
};

