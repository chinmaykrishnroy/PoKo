#pragma once
#include <Arduino.h>

class OneButton {
public:
    using Callback = void (*)();

    void setup(int pin, int mode, bool activeLow = true) {
        _pin = pin;
        _activeLow = activeLow;
        pinMode(pin, mode);
    }
    void setClickMs(unsigned int ms) { _clickMs = ms; }
    void setPressMs(unsigned int ms) { _pressMs = ms; }
    void setDebounceMs(unsigned int) {}
    void setLongPressIntervalMs(unsigned int ms) { _longIntervalMs = ms ? ms : 1; }
    void attachClick(Callback cb) { _click = cb; }
    void attachDoubleClick(Callback cb) { _double = cb; }
    void attachLongPressStart(Callback cb) { _longStart = cb; }
    void attachDuringLongPress(Callback cb) { _duringLong = cb; }

    void reset() {
        _pressed = false;
        _longStarted = false;
        _clickPending = false;
        _secondClick = false;
        _clickCount = 0;
        _pressedAt = _lastRepeatAt = _releasedAt = millis();
    }

    // Host-test overload mirroring OneButton's externally sampled input mode.
    // `active` is true while the logical button is pressed.
    void tick(bool active) {
        const uint32_t now = millis();
        if (active && !_pressed) {
            _pressed = true;
            _longStarted = false;
            _secondClick = _double && _clickPending && (now - _releasedAt <= _clickMs);
            _pressedAt = now;
            _lastRepeatAt = now;
        } else if (active && _pressed) {
            if (!_longStarted && now - _pressedAt >= _pressMs) {
                _longStarted = true;
                _clickPending = false;
                _clickCount = 0;
                _lastRepeatAt = now;
                if (_longStart) _longStart();
            }
            if (_longStarted && _duringLong && now - _lastRepeatAt >= _longIntervalMs) {
                _lastRepeatAt = now;
                _duringLong();
            }
        } else if (!active && _pressed) {
            _pressed = false;
            if (_longStarted) {
                _longStarted = false;
                _clickPending = false;
                _clickCount = 0;
            } else if (!_double) {
                if (_click) _click();
            } else {
                if (_secondClick) {
                    _secondClick = false;
                    _clickCount = 0;
                    _clickPending = false;
                    if (_double) _double();
                } else {
                    _clickCount = 1;
                    _releasedAt = now;
                    _clickPending = true;
                }
            }
        }

        if (!active && !_pressed && _clickPending && now - _releasedAt >= _clickMs) {
            _clickPending = false;
            _clickCount = 0;
            if (_click) _click();
        }
    }

private:
    int _pin = -1;
    bool _activeLow = true;
    bool _pressed = false;
    bool _longStarted = false;
    bool _clickPending = false;
    bool _secondClick = false;
    unsigned int _clickMs = 400;
    unsigned int _pressMs = 800;
    unsigned int _longIntervalMs = 100;
    uint8_t _clickCount = 0;
    uint32_t _pressedAt = 0;
    uint32_t _releasedAt = 0;
    uint32_t _lastRepeatAt = 0;
    Callback _click = nullptr;
    Callback _double = nullptr;
    Callback _longStart = nullptr;
    Callback _duringLong = nullptr;
};
