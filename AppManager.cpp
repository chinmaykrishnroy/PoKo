#include "AppManager.h"
#include "ButtonInput.h"
#include "PokoTheme.h"
#include "PokoDrivers.h"
#include <U8g2lib.h>

// ─────────────────────────────────────────────────────────────
//  AppManager — Implementation
//  All state-switching logic that previously lived as repeated
//  if/else chains in PoKo.ino now lives here exactly once.
// ─────────────────────────────────────────────────────────────

// AppState values that are safe to persist and restore across reboots.
// Streaming apps (Music, Video) are excluded — they need active network.
static bool isSafeRestoreState(AppState s) {
    switch (s) {
        case STATE_CLOCK:
        case STATE_GALLERY_UI:
        case STATE_SETTINGS_UI:
        case STATE_INFO:
        case STATE_PIXELS_UI:
        case STATE_SSYNC:
        case STATE_LAUNCHER:
            return true;
        default:
            return false;
    }
}

AppManager::AppManager(Arduino_GFX* gfx, Preferences& prefs, ButtonInput& btnInput)
    : _gfx(gfx), _prefs(prefs), _btnInput(btnInput) {}

void AppManager::registerApp(AppState state, IApp* app) {
    if (state >= 0 && state < STATE_COUNT) {
        _apps[state] = app;
    }
}

void AppManager::setOnSwitch(SwitchCallback cb) {
    _onSwitch = cb;
}

void AppManager::begin(AppState initialApp) {
    _activeState = initialApp;
    // Configure hold-repeat for the initial app
    IApp* a = _apps[_activeState];
    _btnInput.setHoldRepeatEnabled(a && a->wantsHoldRepeat());
    _btnInput.suppressUntilAllReleased();
}

void AppManager::switchTo(AppState newState) {
    if (newState == _activeState) return;

    Serial.printf("[AppMgr] switch %d -> %d (%s)\n",
                  (int)_activeState, (int)newState,
                  _apps[newState] ? _apps[newState]->appName() : "?");

    // Persist last_app for safe apps only — streaming apps cannot be auto-restored
    if (isSafeRestoreState(newState)) {
        _prefs.putInt("last_app", (int)newState);
    }

    // Unload outgoing app
    IApp* outgoing = _apps[_activeState];
    if (outgoing) outgoing->unload();

    _activeState = newState;

    // Configure hold-repeat based on incoming app's preference
    IApp* incoming = _apps[_activeState];
    _btnInput.setHoldRepeatEnabled(incoming && incoming->wantsHoldRepeat());
    // Do not replay a held gesture into the newly opened application.
    _btnInput.suppressUntilAllReleased();

    // Show themed loading screen between apps to eliminate black-freeze flash
    if (newState == STATE_LAUNCHER) {
        if (_gfx) _gfx->fillScreen(currentTheme().bg);
    } else {
        showLoadingScreen(newState);
    }

    // Load incoming app
    if (incoming) incoming->load();

    // Notify listeners (status bar refresh, etc.)
    if (_onSwitch) _onSwitch(newState);
}

void AppManager::update() {
    IApp* a = _apps[_activeState];
    if (a) a->update();
}

void AppManager::refreshTheme() {
    IApp* a = _apps[_activeState];
    if (a) a->refreshTheme();
}

void AppManager::handleInput(InputEvent e) {
    IApp* a = _apps[_activeState];
    if (a) a->handleInput(e);
}

void AppManager::onDriverReset() {
    IApp* a = _apps[_activeState];
    if (a) a->onDriverReset();
}

void AppManager::onDisplayWake() {
    IApp* a = _apps[_activeState];
    if (a) a->onDisplayWake();
}

AppState AppManager::activeState() const {
    return _activeState;
}

IApp* AppManager::activeApp() const {
    return _apps[_activeState];
}

IApp* AppManager::app(AppState state) const {
    if (state >= 0 && state < STATE_COUNT) return _apps[state];
    return nullptr;
}

// ── Themed Loading Screen ─────────────────────────────────────
// Separate distinct designs for Dark & Light modes.
// Preserves the exact visual behaviour from the original PoKo.ino.
void AppManager::showLoadingScreen(AppState state) {
    if (!_gfx) return;
    const auto& theme = currentTheme();
    bool dark = isDarkTheme();

    IApp* incoming = _apps[state];
    const char* appName = incoming ? incoming->appName() : "App";

    int16_t x1, y1;
    uint16_t w, h;

    if (dark) {
        // ── DARK THEME ────────────────────────────────────────
        // Pure black background, subtle outline border, cyan accent pill.
        _gfx->fillScreen(0x0000);
        // Outline-only rounded rect (no fill — avoids lighter card against black bg)
        _gfx->drawRoundRect(12, 32, 104, 64, 6, 0x18C3);  // dim cyan border
        // Glowing cyan accent bar
        _gfx->fillRoundRect(48, 40, 32, 3, 1, 0x07FF);

        char titleBuf[24];
        snprintf(titleBuf, sizeof(titleBuf), "Loading %s...", appName);
        _gfx->setFont(u8g2_font_helvB08_tf);
        _gfx->setTextColor(0xFFFF, 0x0000);
        _gfx->getTextBounds(titleBuf, 0, 0, &x1, &y1, &w, &h);
        if (w > 92) {
            const char* fallback = "Loading App...";
            _gfx->getTextBounds(fallback, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(64 - w / 2, 62);
            _gfx->print(fallback);
        } else {
            _gfx->setCursor(64 - w / 2, 62);
            _gfx->print(titleBuf);
        }
        _gfx->setFont(u8g2_font_5x7_tf);
        _gfx->setTextColor(0x4208, 0x0000);
        const char* sub = "PLEASE WAIT";
        _gfx->getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 79);
        _gfx->print(sub);

    } else {
        // ── LIGHT THEME ───────────────────────────────────────
        // Pure crisp white background, soft modern off-white card, navy accent.
        _gfx->fillScreen(0xFFFF);
        _gfx->fillRoundRect(12, 32, 104, 64, 6, 0xF7BE); // theme.surface
        _gfx->drawRoundRect(12, 32, 104, 64, 6, 0xCE79); // theme.line border
        // Deep Royal Navy accent bar
        _gfx->fillRoundRect(48, 40, 32, 3, 1, 0x01F4);

        char titleBuf[24];
        snprintf(titleBuf, sizeof(titleBuf), "Loading %s...", appName);
        _gfx->setFont(u8g2_font_helvB08_tf);
        _gfx->setTextColor(0x0000, 0xF7BE);
        _gfx->getTextBounds(titleBuf, 0, 0, &x1, &y1, &w, &h);
        if (w > 92) {
            const char* fallback = "Loading App...";
            _gfx->getTextBounds(fallback, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(64 - w / 2, 62);
            _gfx->print(fallback);
        } else {
            _gfx->setCursor(64 - w / 2, 62);
            _gfx->print(titleBuf);
        }
        _gfx->setFont(u8g2_font_5x7_tf);
        _gfx->setTextColor(0x632C, 0xF7BE);
        const char* sub = "PLEASE WAIT";
        _gfx->getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 79);
        _gfx->print(sub);
    }
}
