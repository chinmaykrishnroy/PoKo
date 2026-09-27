#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  ButtonInput — Wraps OneButton for Boot (GPIO 0) and Key
//  (supports both GPIO 5 and GPIO 4). Also handles dual-button
//  hold combos (2s brightness, 5s reset, 10s reboot).
// ─────────────────────────────────────────────────────────────

class ButtonInput {
public:
    typedef void (*SimpleCb)();

private:
    OneButton _btnLeft;
    OneButton _btnRight1;
    OneButton _btnRight2;

    SimpleCb _onLeft          = nullptr;
    SimpleCb _onRight         = nullptr;
    SimpleCb _onLeftDouble    = nullptr;
    SimpleCb _onRightDouble   = nullptr;
    SimpleCb _onLongLeft      = nullptr;
    SimpleCb _onLongRight     = nullptr;
    SimpleCb _onLeftHolding   = nullptr;
    SimpleCb _onRightHolding  = nullptr;
    SimpleCb _onBothClick     = nullptr;
    SimpleCb _onBothDouble    = nullptr;
    SimpleCb _onBothLong      = nullptr;
    SimpleCb _onBothVLong     = nullptr;
    SimpleCb _onBothUltra     = nullptr;

    // Single-button continuous hold tracking (e.g. volume ramping)
    uint32_t _leftHoldStartMs   = 0;
    uint32_t _leftLastRepeatMs  = 0;
    uint32_t _rightHoldStartMs  = 0;
    uint32_t _rightLastRepeatMs = 0;

    // Dual-button combo & click tracking
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

    static void _cbClickLeft() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLeft) {
            Serial.println("[btn] Left (BOOT) Click");
            _instance->_onLeft();
        }
    }
    static void _cbDblClickLeft() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLeftDouble) {
            Serial.println("[btn] Left (BOOT) Double-Click");
            _instance->_onLeftDouble();
        }
    }
    static void _cbLongLeft() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLongLeft) {
            Serial.println("[btn] Left (BOOT) Long-Press");
            _instance->_onLongLeft();
        }
    }

    static void _cbClickRight() {
        if (_instance && !_instance->_suppressSingle && _instance->_onRight) {
            Serial.println("[btn] Right (KEY) Click");
            _instance->_onRight();
        }
    }
    static void _cbDblClickRight() {
        if (_instance && !_instance->_suppressSingle && _instance->_onRightDouble) {
            Serial.println("[btn] Right (KEY) Double-Click");
            _instance->_onRightDouble();
        }
    }
    static void _cbLongRight() {
        if (_instance && !_instance->_suppressSingle && _instance->_onLongRight) {
            Serial.println("[btn] Right (KEY) Long-Press");
            _instance->_onLongRight();
        }
    }

public:
    ButtonInput()
        : _btnLeft(POKO_PIN_BTN_LEFT, true),
          _btnRight1(POKO_PIN_BTN_RIGHT1, true),
          _btnRight2(POKO_PIN_BTN_RIGHT2, true) {
        _instance = this;
    }

    void begin() {
        // Adjust timings: 400ms double click window for comfortable double-tapping
        _btnLeft.setClickMs(400);
        _btnLeft.setPressMs(750);
        _btnLeft.setDebounceMs(20);

        _btnRight1.setClickMs(400);
        _btnRight1.setPressMs(750);
        _btnRight1.setDebounceMs(20);

        _btnRight2.setClickMs(400);
        _btnRight2.setPressMs(750);
        _btnRight2.setDebounceMs(20);

        _btnLeft.attachClick(_cbClickLeft);
        _btnLeft.attachDoubleClick(_cbDblClickLeft);
        _btnLeft.attachLongPressStart(_cbLongLeft);

        // Attach both pin 5 and pin 4 to Right button callbacks
        _btnRight1.attachClick(_cbClickRight);
        _btnRight1.attachDoubleClick(_cbDblClickRight);
        _btnRight1.attachLongPressStart(_cbLongRight);

        _btnRight2.attachClick(_cbClickRight);
        _btnRight2.attachDoubleClick(_cbDblClickRight);
        _btnRight2.attachLongPressStart(_cbLongRight);

        Serial.println("[btn] OneButton initialized (Left=0, Right=5/4)");
    }

    void onLeft(SimpleCb cb)          { _onLeft          = cb; }
    void onRight(SimpleCb cb)         { _onRight         = cb; }
    void onLeftDouble(SimpleCb cb)    { _onLeftDouble    = cb; }
    void onRightDouble(SimpleCb cb)   { _onRightDouble   = cb; }
    void onLongLeft(SimpleCb cb)      { _onLongLeft      = cb; }
    void onLongRight(SimpleCb cb)     { _onLongRight     = cb; }
    void onLeftHolding(SimpleCb cb)   { _onLeftHolding   = cb; }
    void onRightHolding(SimpleCb cb)  { _onRightHolding  = cb; }
    void onBothClick(SimpleCb cb)     { _onBothClick     = cb; }
    void onBothDouble(SimpleCb cb)    { _onBothDouble    = cb; }
    void onBothLong(SimpleCb cb)      { _onBothLong      = cb; }
    void onBothVLong(SimpleCb cb)     { _onBothVLong     = cb; }
    void onBothUltra(SimpleCb cb)     { _onBothUltra     = cb; }

    void update() {
        bool lPressed = (digitalRead(POKO_PIN_BTN_LEFT) == LOW);
        bool rPressed = (digitalRead(POKO_PIN_BTN_RIGHT1) == LOW) ||
                        (digitalRead(POKO_PIN_BTN_RIGHT2) == LOW);

        uint32_t now = millis();

        if (lPressed && rPressed) {
            // Both buttons are held together
            _leftHoldStartMs = 0;
            _rightHoldStartMs = 0;

            if (!_comboHolding) {
                _comboHolding    = true;
                _comboStartMs    = now;
                _comboLongFired  = false;
                _comboVLongFired = false;
                _comboUltraFired = false;
                _suppressSingle  = true;
                _dualCandidate   = true;
                // Suppress OneButton so single-button clicks/longpresses don't fire
                _btnLeft.reset();
                _btnRight1.reset();
                _btnRight2.reset();
            } else {
                uint32_t held = now - _comboStartMs;
                if (!_comboLongFired && held >= 2000) {
                    _comboLongFired = true;
                    _dualCandidate  = false;
                    _dualClickCount = 0;
                    Serial.println("[combo] Both held 2s");
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
                // Just released from dual-button press
                _comboHolding = false;
                _btnLeft.reset();
                _btnRight1.reset();
                _btnRight2.reset();

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
            if (!lPressed && !rPressed) {
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
                if (lPressed && !rPressed) {
                    if (_leftHoldStartMs == 0) {
                        _leftHoldStartMs = now;
                        _leftLastRepeatMs = now;
                    } else if (now - _leftHoldStartMs >= 450) {
                        if (now - _leftLastRepeatMs >= 100) {
                            _leftLastRepeatMs = now;
                            if (_onLeftHolding) _onLeftHolding();
                        }
                    }
                } else {
                    _leftHoldStartMs = 0;
                }

                if (rPressed && !lPressed) {
                    if (_rightHoldStartMs == 0) {
                        _rightHoldStartMs = now;
                        _rightLastRepeatMs = now;
                    } else if (now - _rightHoldStartMs >= 450) {
                        if (now - _rightLastRepeatMs >= 100) {
                            _rightLastRepeatMs = now;
                            if (_onRightHolding) _onRightHolding();
                        }
                    }
                } else {
                    _rightHoldStartMs = 0;
                }
            }
        }

        // Only tick OneButton if not in dual-button combo mode
        if (!_comboHolding && !_suppressSingle) {
            _btnLeft.tick();
            _btnRight1.tick();
            _btnRight2.tick();
        }
    }
};

inline ButtonInput* ButtonInput::_instance = nullptr;
