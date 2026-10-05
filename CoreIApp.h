#pragma once
#include <Arduino.h>
#include "CoreInputEvent.h"

// ─────────────────────────────────────────────────────────────
//  IApp — Pure virtual interface for all PoKo applications
//
//  Every app (InfoApp, ClockApp, MusicApp, VideoApp, …) implements
//  this interface. AppManager dispatches all lifecycle, input, and
//  rendering calls through it, eliminating the if/else chains in
//  PoKo.ino that previously duplicated the same dispatch 8 times.
//
//  Design notes:
//  - begin()   : called once at startup (allocate persistent state)
//  - load()    : called when the app becomes foreground; draw initial frame
//  - unload()  : called when leaving; release transient resources
//  - update()  : called every loop() iteration while app is foreground
//  - handleInput() : all button gestures route here; app decides meaning
//  - refreshTheme(): called after dark/light theme toggle
//  - onDriverReset(): called after a hardware driver reset; default re-loads
//  - onDisplayWake(): called when display wakes from sleep; default re-renders
// ─────────────────────────────────────────────────────────────

class IApp {
public:
    virtual ~IApp() = default;

    // ── Lifecycle ─────────────────────────────────────────────
    /// One-time initialisation at boot. Allocate long-lived buffers here.
    virtual void begin() = 0;

    /// App is now foreground. Draw initial frame, start I/O, acquire resources.
    virtual void load() = 0;

    /// App is leaving foreground. Release transient resources, stop I/O.
    virtual void unload() = 0;

    /// Called every loop() while this app is foreground. Keep non-blocking.
    virtual void update() = 0;

    // ── Rendering ─────────────────────────────────────────────
    /// Re-render the current state into the canvas and flush. Default: no-op.
    virtual void renderToCanvas() {}

    /// Theme has changed (dark ↔ light). Repaint with new colours. Default: no-op.
    virtual void refreshTheme() {}

    // ── Input Routing ─────────────────────────────────────────
    /// Route a typed input gesture to this app.
    virtual void handleInput(InputEvent e) = 0;

    // ── Metadata ──────────────────────────────────────────────
    /// Human-readable name used in loading screen and logs.
    virtual const char* appName() const = 0;

    /// True if this app uses hold-repeat gestures (volume, pixel colour adjust).
    virtual bool wantsHoldRepeat() const { return false; }

    /// True if AppManager should persist this app as "last_app" across reboots.
    /// Streaming apps (Music, Video) return false — they require active network.
    virtual bool isSafeForRestore() const { return true; }

    // ── Hardware Events ────────────────────────────────────────
    /// Driver reset was performed. Default: re-invoke load() to repaint.
    virtual void onDriverReset() { load(); }

    /// Display woke from sleep. Default: re-invoke renderToCanvas().
    virtual void onDisplayWake() { renderToCanvas(); }
};
