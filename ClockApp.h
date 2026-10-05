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
#include "CoreIApp.h"

extern PowerManager* powerManager;

// ─────────────────────────────────────────────────────────────
//  ClockApp — Digital Clock (128×128) with NTP sync (IST)
//  Uses Arduino_Canvas for zero-flicker double-buffered display.
// ─────────────────────────────────────────────────────────────

class ClockApp : public IApp {
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

    // IApp interface
    void begin()    override;
    void load()     override;
    void unload()   override;
    void update()   override;
    void refreshTheme() override;
    const char* appName() const override { return "Clock"; }
    bool isSafeForRestore() const override { return true; }
    void handleInput(InputEvent e) override;
    void onDisplayWake() override { load(); } // ClockApp re-loads on wake (NTP refresh)

    bool isLoaded() const;
    void onLeft();
    void onRight();
    void onBack();
    void onEnter();
};

