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
    SimpleCb _onBothClick     = nullptr;
    SimpleCb _onBothDouble    = nullptr;
    SimpleCb _onBothLong      = nullptr;
    SimpleCb _onBothVLong     = nullptr;
    SimpleCb _onBothUltra     = nullptr;

    // Dual-button combo tracking
    bool     _comboHolding     = false;
    bool     _comboLongFired   = false;
    bool     _comboVLongFired  = false;
    bool     _comboUltraFired  = false;
    uint32_t _comboStartMs     = 0;

    static ButtonInput* _instance;

    static void _cbClickLeft() {
        if (_instance && _instance->_onLeft) {
            Serial.println("[btn] Left (BOOT) Click");
            _instance->_onLeft();
        }
    }
    static void _cbDblClickLeft() {
        if (_instance && _instance->_onLeftDouble) {
            Serial.println("[btn] Left (BOOT) Double-Click");
            _instance->_onLeftDouble();
        }
    }
    static void _cbLongLeft() {
        if (_instance && _instance->_onLongLeft) {
            Serial.println("[btn] Left (BOOT) Long-Press");
            _instance->_onLongLeft();
        }
    }

    static void _cbClickRight() {
        if (_instance && _instance->_onRight) {
            Serial.println("[btn] Right (KEY) Click");
            _instance->_onRight();
        }
    }
    static void _cbDblClickRight() {
        if (_instance && _instance->_onRightDouble) {
            Serial.println("[btn] Right (KEY) Double-Click");
            _instance->_onRightDouble();
        }
    }
    static void _cbLongRight() {
        if (_instance && _instance->_onLongRight) {
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
    void onBothClick(SimpleCb cb)     { _onBothClick     = cb; }
    void onBothDouble(SimpleCb cb)    { _onBothDouble    = cb; }
    void onBothLong(SimpleCb cb)      { _onBothLong      = cb; }
    void onBothVLong(SimpleCb cb)     { _onBothVLong     = cb; }
    void onBothUltra(SimpleCb cb)     { _onBothUltra     = cb; }

    void update() {
        _btnLeft.tick();
        _btnRight1.tick();
        _btnRight2.tick();

        // Dual-button hold detection
        bool lPressed = (digitalRead(POKO_PIN_BTN_LEFT) == LOW);
        bool rPressed = (digitalRead(POKO_PIN_BTN_RIGHT1) == LOW) ||
                        (digitalRead(POKO_PIN_BTN_RIGHT2) == LOW);

        uint32_t now = millis();
        if (lPressed && rPressed) {
            if (!_comboHolding) {
                _comboHolding    = true;
                _comboStartMs    = now;
                _comboLongFired  = false;
                _comboVLongFired = false;
                _comboUltraFired = false;
            } else {
                uint32_t held = now - _comboStartMs;
                if (!_comboLongFired && held >= 2000) {
                    _comboLongFired = true;
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
            _comboHolding = false;
        }
    }
};

inline ButtonInput* ButtonInput::_instance = nullptr;
