#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include "PokoAppState.h"
#include "CoreIApp.h"
#include "CoreInputEvent.h"

// Forward-declare ButtonInput so AppManager can call suppressUntilAllReleased()
// without pulling the full ButtonInput header into every translation unit.
class ButtonInput;

// ─────────────────────────────────────────────────────────────
//  AppManager — Central application dispatcher
//
//  Owns an array of IApp* pointers indexed by AppState.
//  All app lifecycle, input routing, theme, and driver-reset calls
//  flow through AppManager. PoKo.ino's loop() and setup() no longer
//  contain any app-specific if/else chains.
//
//  Usage in setup():
//      appMgr.registerApp(STATE_INFO,   infoApp);
//      appMgr.registerApp(STATE_CLOCK,  clockApp);
//      // ... all apps registered ...
//      appMgr.begin(initialApp);
//
//  Usage in loop():
//      appMgr.update();
//      // input callbacks call appMgr.handleInput(InputEvent::Left) etc.
// ─────────────────────────────────────────────────────────────

class AppManager {
public:
    /// Callback fired after every successful app switch.
    using SwitchCallback = void (*)(AppState newState);

    AppManager(Arduino_GFX* gfx, Preferences& prefs, ButtonInput& btnInput);

    /// Register a concrete app for a given state slot. Call before begin().
    void registerApp(AppState state, IApp* app);

    /// Set callback invoked after every switch (used by status bar refresh).
    void setOnSwitch(SwitchCallback cb);

    /// Complete registration and activate initialApp (does not call load()).
    /// The .ino renders the initial frame itself before handing off to the loop.
    void begin(AppState initialApp);

    /// Switch to a different app: unload current, loading screen, load new.
    void switchTo(AppState newState);

    /// Call every loop() iteration — drives the foreground app's update().
    void update();

    /// Propagate theme change to the foreground app.
    void refreshTheme();

    /// Route a typed input gesture to the foreground app.
    void handleInput(InputEvent e);

    /// Notify foreground app of a hardware driver reset.
    void onDriverReset();

    /// Notify foreground app that the display just woke from sleep.
    void onDisplayWake();

    /// Current foreground AppState.
    AppState activeState() const;

    /// Pointer to the foreground IApp (nullptr if none registered).
    IApp* activeApp() const;

    /// Pointer to a registered app by state (nullptr if not registered).
    IApp* app(AppState state) const;

private:
    void showLoadingScreen(AppState state);

    Arduino_GFX*    _gfx;
    Preferences&    _prefs;
    ButtonInput&    _btnInput;
    SwitchCallback  _onSwitch   = nullptr;
    AppState        _activeState = STATE_LAUNCHER;
    IApp*           _apps[STATE_COUNT] = {};
};
