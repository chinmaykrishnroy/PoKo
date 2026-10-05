#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"
#include "PokoTheme.h"
#include "SnapPlayer.h"
#include "AudioManager.h"
#include "CoreIApp.h"

// ─────────────────────────────────────────────────────────────
//  SSyncApp — Direct Snapcast Client UI (128×128)
//  Connects to the configured Snapcast server on port 1704 by default
//  with client name PoKo and full ESP-IDF 5.x I2S audio playback.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class SSyncApp : public IApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;
    SnapPlayer*     _player = nullptr;

    bool     _active     = false;
    bool     _dirty      = true;
    uint32_t _lastDrawMs = 0;

public:
    void renderToCanvas() override;

public:
    SSyncApp(Arduino_GFX* gfx, AppSwitchFn exitFn, SnapPlayer* player = nullptr);

    void setPlayer(SnapPlayer* p);

    // IApp interface
    void begin()    override;
    void load()     override;
    void unload()   override;
    void update()   override;
    void refreshTheme() override;
    const char* appName() const override { return "SSync"; }
    bool wantsHoldRepeat() const override { return true; }
    bool isSafeForRestore() const override { return true; }
    void handleInput(InputEvent e) override;
    void onDisplayWake() override { renderToCanvas(); }

    bool isLoaded() const;
    bool isPlaying() const;
    SnapPlayer* getPlayer();

    void onLeft();
    void onRight();
    void volumeRampDown(int step = 2);
    void volumeRampUp(int step = 2);
    void onBack();
    void onEnter();
    void onLongRight();

    // REST API status
    String apiJson();
};

