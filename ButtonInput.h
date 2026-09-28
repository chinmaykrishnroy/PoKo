#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  ButtonInput — Physical button management for PoKo
//  Hardware:
//    BOOT/DOWN: GPIO 0 (Left / Down navigation)
//    PLUS/UP:   GPIO 4 (Right / Up navigation / Select)
//    PWR:       GPIO 5 (Dedicated System Power / Sleep / Wake)
// ─────────────────────────────────────────────────────────────

class ButtonInput {
public:
    typedef void (*SimpleCb)();

private:
    OneButton _btnDown;
    OneButton _btnUp;
    OneButton _btnPwr;

    SimpleCb _onDown         = nullptr;
    SimpleCb _onUp           = nullptr;
    SimpleCb _onDownDouble   = nullptr;
    SimpleCb _onUpDouble     = nullptr;
    SimpleCb _onLongDown     = nullptr;
    SimpleCb _onLongUp       = nullptr;
    SimpleCb _onDownHolding  = nullptr;
    SimpleCb _onUpHolding    = nullptr;

    SimpleCb _onPwrClick     = nullptr;
    SimpleCb _onPwrDouble    = nullptr;
    SimpleCb _onPwrLong      = nullptr;

    SimpleCb _onBothClick    = nullptr;
    SimpleCb _onBothDouble   = nullptr;
    SimpleCb _onBothLong     = nullptr;
    SimpleCb _onBothVLong    = nullptr;
    SimpleCb _onBothUltra    = nullptr;

    // Single-button continuous hold tracking (e.g. volume ramping)
    uint32_t _downHoldStartMs   = 0;
    uint32_t _downLastRepeatMs  = 0;
    uint32_t _upHoldStartMs     = 0;
    uint32_t _upLastRepeatMs    = 0;

    // Dual-button combo tracking (DOWN + UP held simultaneously)
    bool     _comboHolding       = false;
    bool     _comboLongFired     = false;
    bool     _comboVLongFired    = false;
    bool     _comboUltraFired    = false;
    uint32_t _comboStartMs       = 0;
    bool     _suppressSingle     = false;

    // Dual-button short click / double click detection
    uint8_t  _dualClickCount     = 0;
    uint32_t _lastDualReleaseMs  = 0;
    bool     _dualCandidate      = false;

    static ButtonInput* _instance;

    static void _cbClickDown() {
        if (_instance && !_instance->_suppressSingle && _instance->_onDown) {
            Serial.println("[btn] DOWN Click");
            _instance->_onDown();
        }
    }
    static void _cbDblClickDown() {
        if (_instance && !_instance->_suppressSingle && _instance->_onDownDouble) {
            Serial.println("[btn] DOWN Double-Click");
            _instance->_onDownDouble();
        }
    }
    static void _cbLongDown() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLongDown) {
            Serial.println("[btn] DOWN Long-Press");
            _instance->_onLongDown();
        }
    }

    static void _cbClickUp() {
        if (_instance && !_instance->_suppressSingle && _instance->_onUp) {
            Serial.println("[btn] UP Click");
            _instance->_onUp();
        }
    }
    static void _cbDblClickUp() {
        if (_instance && !_instance->_suppressSingle && _instance->_onUpDouble) {
            Serial.println("[btn] UP Double-Click");
            _instance->_onUpDouble();
        }
    }
    static void _cbLongUp() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLongUp) {
            Serial.println("[btn] UP Long-Press");
            _instance->_onLongUp();
        }
    }

    static void _cbClickPwr() {
        if (_instance && _instance->_onPwrClick) {
            Serial.println("[btn] PWR Click");
            _instance->_onPwrClick();
        }
    }
    static void _cbDblClickPwr() {
        if (_instance && _instance->_onPwrDouble) {
            Serial.println("[btn] PWR Double-Click");
            _instance->_onPwrDouble();
        }
    }
    static void _cbLongPwr() {
        if (_instance && _instance->_onPwrLong) {
            Serial.println("[btn] PWR Long-Press (Shutdown)");
            _instance->_onPwrLong();
        }
    }

public:
    ButtonInput()
        : _btnDown(POKO_PIN_BTN_DOWN, true),
          _btnUp(POKO_PIN_BTN_UP, true),
          _btnPwr(POKO_PIN_BTN_PWR, true) {
        _instance = this;
    }

    void begin() {
        // DOWN & UP navigation button timings
        _btnDown.setClickMs(350);
        _btnDown.setPressMs(650);
        _btnDown.setDebounceMs(20);

        _btnUp.setClickMs(350);
        _btnUp.setPressMs(650);
        _btnUp.setDebounceMs(20);

        // PWR button timings: 2000ms pressMs for long-press power off
        _btnPwr.setClickMs(300);
        _btnPwr.setPressMs(2000);
        _btnPwr.setDebounceMs(20);

        // Attach callbacks
        _btnDown.attachClick(_cbClickDown);
        _btnDown.attachDoubleClick(_cbDblClickDown);
        _btnDown.attachLongPressStart(_cbLongDown);

        _btnUp.attachClick(_cbClickUp);
        _btnUp.attachDoubleClick(_cbDblClickUp);
        _btnUp.attachLongPressStart(_cbLongUp);

        _btnPwr.attachClick(_cbClickPwr);
        _btnPwr.attachDoubleClick(_cbDblClickPwr);
        _btnPwr.attachLongPressStart(_cbLongPwr);

        Serial.println("[btn] OneButton initialized (DOWN=GPIO0, UP=GPIO4, PWR=GPIO5)");
    }

    // New semantic setters
    void onDown(SimpleCb cb)          { _onDown        = cb; }
    void onUp(SimpleCb cb)            { _onUp          = cb; }
    void onDownDouble(SimpleCb cb)    { _onDownDouble  = cb; }
    void onUpDouble(SimpleCb cb)      { _onUpDouble    = cb; }
    void onLongDown(SimpleCb cb)      { _onLongDown    = cb; }
    void onLongUp(SimpleCb cb)        { _onLongUp      = cb; }
    void onDownHolding(SimpleCb cb)   { _onDownHolding = cb; }
    void onUpHolding(SimpleCb cb)     { _onUpHolding   = cb; }

    void onPwrClick(SimpleCb cb)      { _onPwrClick    = cb; }
    void onPwrDouble(SimpleCb cb)     { _onPwrDouble   = cb; }
    void onPwrLong(SimpleCb cb)       { _onPwrLong     = cb; }

    // Dual-button combos (DOWN + UP)
    void onBothClick(SimpleCb cb)     { _onBothClick   = cb; }
    void onBothDouble(SimpleCb cb)    { _onBothDouble  = cb; }
    void onBothLong(SimpleCb cb)      { _onBothLong    = cb; }
    void onBothVLong(SimpleCb cb)     { _onBothVLong   = cb; }
    void onBothUltra(SimpleCb cb)     { _onBothUltra   = cb; }

    // Backward-compatibility aliases
    void onLeft(SimpleCb cb)          { _onDown        = cb; }
    void onRight(SimpleCb cb)         { _onUp          = cb; }
    void onLeftDouble(SimpleCb cb)    { _onDownDouble  = cb; }
    void onRightDouble(SimpleCb cb)   { _onUpDouble    = cb; }
    void onLongLeft(SimpleCb cb)      { _onLongDown    = cb; }
    void onLongRight(SimpleCb cb)     { _onLongUp      = cb; }
    void onLeftHolding(SimpleCb cb)   { _onDownHolding = cb; }
    void onRightHolding(SimpleCb cb)  { _onUpHolding   = cb; }

    void reset() {
        _btnDown.reset();
        _btnUp.reset();
        _btnPwr.reset();
        _downHoldStartMs = 0;
        _upHoldStartMs = 0;
        _comboHolding = false;
        _suppressSingle = false;
    }

    void update() {
        bool downPressed = (digitalRead(POKO_PIN_BTN_DOWN) == LOW);
        bool upPressed   = (digitalRead(POKO_PIN_BTN_UP) == LOW);

        uint32_t now = millis();

        if (downPressed && upPressed) {
            // Both DOWN and UP buttons are held together
            _downHoldStartMs = 0;
            _upHoldStartMs = 0;

            if (!_comboHolding) {
                _comboHolding    = true;
                _comboStartMs    = now;
                _comboLongFired  = false;
                _comboVLongFired = false;
                _comboUltraFired = false;
                _suppressSingle  = true;
                _dualCandidate   = true;
                _btnDown.reset();
                _btnUp.reset();
            } else {
                uint32_t held = now - _comboStartMs;
                if (!_comboLongFired && held >= 2500) {
                    _comboLongFired = true;
                    _dualCandidate  = false;
                    _dualClickCount = 0;
                    Serial.println("[combo] Both held 2.5s (Reboot)");
                    if (_onBothLong) _onBothLong();
                }
                if (!_comboVLongFired && held >= 5000) {
                    _comboVLongFired = true;
                    Serial.println("[combo] Both held 5s");
                    if (_onBothVLong) _onBothVLong();
                }
                if (!_comboUltraFired && held >= 10000) {
                    _comboUltraFired = true;
                    Serial.println("[combo] Both held 10s");
                    if (_onBothUltra) _onBothUltra();
                }
            }
        } else {
            // At least one button is NOT pressed
            if (_comboHolding) {
                _comboHolding = false;
                _btnDown.reset();
                _btnUp.reset();

                // If released before 2s hold (and within 600ms of dual press), it's a dual click candidate
                if (_dualCandidate && !_comboLongFired && (now - _comboStartMs < 600)) {
                    _dualClickCount++;
                    _lastDualReleaseMs = now;
                    if (_dualClickCount >= 2) {
                        Serial.println("[combo] Both Double-Click");
                        _dualClickCount = 0;
                        _dualCandidate = false;
                        if (_onBothDouble) _onBothDouble();
                    }
                } else {
                    _dualCandidate = false;
                    _dualClickCount = 0;
                }
            }

            // Only clear suppression when both buttons are fully released
            if (!downPressed && !upPressed) {
                _suppressSingle = false;
            }

            // Check if dual single-click pending window expired
            if (_dualClickCount == 1 && (now - _lastDualReleaseMs > 350)) {
                Serial.println("[combo] Both Click");
                _dualClickCount = 0;
                _dualCandidate = false;
                if (_onBothClick) _onBothClick();
            }

            // Single button continuous press-and-hold (e.g. volume ramp)
            if (!_suppressSingle) {
                if (downPressed && !upPressed) {
                    if (_downHoldStartMs == 0) {
                        _downHoldStartMs = now;
                        _downLastRepeatMs = now;
                    } else if (now - _downHoldStartMs >= 450) {
                        if (now - _downLastRepeatMs >= 100) {
                            _downLastRepeatMs = now;
                            if (_onDownHolding) _onDownHolding();
                        }
                    }
                } else {
                    _downHoldStartMs = 0;
                }

                if (upPressed && !downPressed) {
                    if (_upHoldStartMs == 0) {
                        _upHoldStartMs = now;
                        _upLastRepeatMs = now;
                    } else if (now - _upHoldStartMs >= 450) {
                        if (now - _upLastRepeatMs >= 100) {
                            _upLastRepeatMs = now;
                            if (_onUpHolding) _onUpHolding();
                        }
                    }
                } else {
                    _upHoldStartMs = 0;
                }
            }
        }

        // Tick OneButton instances
        if (!_comboHolding && !_suppressSingle) {
            _btnDown.tick();
            _btnUp.tick();
        }
        _btnPwr.tick();
    }
};

inline ButtonInput* ButtonInput::_instance = nullptr;
