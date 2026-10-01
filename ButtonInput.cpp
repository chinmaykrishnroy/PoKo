#include "ButtonInput.h"

ButtonInput* ButtonInput::_instance = nullptr;

void ButtonInput::emit(Kind kind) {
        uint32_t head = _head.load(std::memory_order_relaxed);
        uint32_t next = (head + 1) % QUEUE_SIZE;
        if (next == _tail.load(std::memory_order_acquire)) {
            _overflow.store(true, std::memory_order_release);
            return;
        }
        _events[head] = {kind, _sampleGeneration};
        _head.store(next, std::memory_order_release);
    }

bool ButtonInput::pop(Event& event) {
        uint32_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == _head.load(std::memory_order_acquire)) return false;
        event = _events[tail];
        _tail.store((tail + 1) % QUEUE_SIZE, std::memory_order_release);
        return true;
    }

void ButtonInput::resetButton(OneButton& button) {
        // OneButton::reset() leaves its internal debounced level unchanged.
        // Prime it inactive before resuming after a consumed hold/chord.
        button.reset();
        button.tick(false);
        button.tick(false);
        button.reset();
    }

void ButtonInput::resetRecognizers() {
        resetButton(_btnDown); resetButton(_btnUp); resetButton(_btnPwr);
        _combo = _comboReleasing = false;
        _dualClicks = 0;
    }

#ifdef ARDUINO_ARCH_ESP32
void ButtonInput::task(void* context) {
        auto* self = static_cast<ButtonInput*>(context);
        TickType_t last = xTaskGetTickCount();
        for (;;) {
            self->sample();
            vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
        }
    }
#endif

ButtonInput::ButtonInput() { _instance = this; }

void ButtonInput::begin() {
#ifdef ARDUINO_ARCH_ESP32
        for (int pin : {POKO_PIN_BTN_DOWN, POKO_PIN_BTN_UP, POKO_PIN_BTN_PWR}) {
            rtc_gpio_deinit((gpio_num_t)pin);
        }
#endif
        _btnDown.setup(POKO_PIN_BTN_DOWN, INPUT_PULLUP, true);
        _btnUp.setup(POKO_PIN_BTN_UP, INPUT_PULLUP, true);
        _btnPwr.setup(POKO_PIN_BTN_PWR, INPUT_PULLUP, true);
        for (OneButton* button : {&_btnDown, &_btnUp}) {
            button->setClickMs(BUTTON_CLICK_MS);
            button->setPressMs(BUTTON_PRESS_MS);
            // sample() debounces all pins once, shared with chord recognition.
            button->setDebounceMs(0);
            button->setLongPressIntervalMs(100);
        }
        _btnPwr.setClickMs(PWR_CLICK_MS);
        _btnPwr.setPressMs(PWR_PRESS_MS);
        _btnPwr.setDebounceMs(0);
        _btnDown.attachClick(callback<Down>);
        _btnUp.attachClick(callback<Up>);
        _btnDown.attachDoubleClick(callback<DownDouble>);
        _btnUp.attachDoubleClick(callback<UpDouble>);
        _btnDown.attachLongPressStart(callback<LongDown>);
        _btnUp.attachLongPressStart(callback<LongUp>);
        _btnDown.attachDuringLongPress(callback<RepeatDown>);
        _btnUp.attachDuringLongPress(callback<RepeatUp>);
        _btnPwr.attachClick(callback<Power>);
        if (_callbacks[PowerDouble]) _btnPwr.attachDoubleClick(callback<PowerDouble>);
        _btnPwr.attachLongPressStart(callback<PowerLong>);
#ifdef ARDUINO_ARCH_ESP32
        // Consume a button still held from boot/deep-sleep wake.
        _suppressed = true;
        _taskStarted = xTaskCreatePinnedToCore(task, "PokoInput", 4096, this, 3, nullptr, 1) == pdPASS;
        if (!_taskStarted) Serial.println("[btn] input task unavailable; using loop polling");
#endif
    }

void ButtonInput::onPress(WakeCb cb) { _wake = cb; }

void ButtonInput::setHoldRepeatEnabled(bool enabled) { _repeatEnabled = enabled; }

void ButtonInput::onDown(SimpleCb cb) { _callbacks[Down] = cb; }

void ButtonInput::onUp(SimpleCb cb) { _callbacks[Up] = cb; }

void ButtonInput::onDownDouble(SimpleCb cb) { _callbacks[DownDouble] = cb; }

void ButtonInput::onUpDouble(SimpleCb cb) { _callbacks[UpDouble] = cb; }

void ButtonInput::onLongDown(SimpleCb cb) { _callbacks[LongDown] = cb; }

void ButtonInput::onLongUp(SimpleCb cb) { _callbacks[LongUp] = cb; }

void ButtonInput::onDownHolding(SimpleCb cb) { _callbacks[RepeatDown] = cb; }

void ButtonInput::onUpHolding(SimpleCb cb) { _callbacks[RepeatUp] = cb; }

void ButtonInput::onPwrClick(SimpleCb cb) { _callbacks[Power] = cb; }

void ButtonInput::onPwrDouble(SimpleCb cb) { _callbacks[PowerDouble] = cb; }

void ButtonInput::onPwrLong(SimpleCb cb) { _callbacks[PowerLong] = cb; }

void ButtonInput::onBothClick(SimpleCb cb) { _callbacks[Both] = cb; }

void ButtonInput::onBothDouble(SimpleCb cb) { _callbacks[BothDouble] = cb; }

void ButtonInput::onBothLong(SimpleCb cb) { _callbacks[BothLong] = cb; }

void ButtonInput::onBothVLong(SimpleCb cb) { _callbacks[BothVLong] = cb; }

void ButtonInput::onBothUltra(SimpleCb cb) { _callbacks[BothUltra] = cb; }

void ButtonInput::onLeft(SimpleCb cb) { onDown(cb); }

void ButtonInput::onRight(SimpleCb cb) { onUp(cb); }

void ButtonInput::onLeftDouble(SimpleCb cb) { onDownDouble(cb); }

void ButtonInput::onRightDouble(SimpleCb cb) { onUpDouble(cb); }

void ButtonInput::onLongLeft(SimpleCb cb) { onLongDown(cb); }

void ButtonInput::onLongRight(SimpleCb cb) { onLongUp(cb); }

void ButtonInput::onLeftHolding(SimpleCb cb) { onDownHolding(cb); }

void ButtonInput::onRightHolding(SimpleCb cb) { onUpHolding(cb); }

void ButtonInput::suppressUntilAllReleased() {
        _generation.fetch_add(1, std::memory_order_acq_rel);
        // A concurrent producer can still enqueue an old-generation event; the
        // consumer rejects it below. Only the worker resets the recognizers.
        _tail.store(_head.load(std::memory_order_acquire), std::memory_order_release);
    }

void ButtonInput::reset() { suppressUntilAllReleased(); }

void ButtonInput::sample() {
        const uint32_t now = millis();
        const int pins[3] = {POKO_PIN_BTN_DOWN, POKO_PIN_BTN_UP, POKO_PIN_BTN_PWR};
        bool pressEdge = false;
        for (int i = 0; i < 3; ++i) {
            bool raw = digitalRead(pins[i]) == LOW;
            if (raw != _raw[i]) { _raw[i] = raw; _changedAt[i] = now; }
            if (_pressed[i] != raw && now - _changedAt[i] >= BUTTON_DEBOUNCE_MS) {
                _pressed[i] = raw;
                pressEdge |= raw;
            }
        }
        uint32_t generation = _generation.load(std::memory_order_acquire);
        if (_sampleGeneration != generation) {
            _sampleGeneration = generation;
            resetRecognizers();
            _suppressed = true;
            _releaseTiming = false;
        }
        if (_suppressed) {
            if (_raw[0] || _raw[1] || _raw[2] || _pressed[0] || _pressed[1] || _pressed[2]) {
                _releaseTiming = false;
            } else if (!_releaseTiming) {
                _releaseTiming = true; _releasedAt = now;
            } else if (now - _releasedAt >= BUTTON_CLICK_MS) {
                resetRecognizers();
                _suppressed = false;
            }
            return;
        }
        if (pressEdge) emit(Press);
        bool down = _pressed[0], up = _pressed[1];
        // Expire a pending chord before accepting another outside its window.
        if (!_combo && _dualClicks == 1 && now - _dualReleased >= DUAL_CLICK_MS) {
            _dualClicks = 0; emit(Both);
        }
        if (down && up && !_comboReleasing) {
            if (!_combo) {
                _combo = true; _comboStart = now;
                resetButton(_btnDown); resetButton(_btnUp);
            }
        } else if (_combo) {
            _combo = false;
            _comboReleasing = true;
            uint32_t held = now - _comboStart;
            if (held >= 2500) {
                _dualClicks = 0;
                emit(held >= 10000 ? BothUltra : held >= 5000 ? BothVLong : BothLong);
            } else if (held < DUAL_CLICK_MS) {
                _dualReleased = now;
                if (++_dualClicks == 2) { _dualClicks = 0; emit(BothDouble); }
            } else _dualClicks = 0;
        }
        if (_comboReleasing) {
            // Reset throughout staggered release; the remaining key must not
            // become a fresh single click/hold after a chord.
            resetButton(_btnDown); resetButton(_btnUp);
            if (!down && !up) _comboReleasing = false;
        } else if (!_combo) {
            _btnDown.tick(down);
            _btnUp.tick(up);
        }
        _btnPwr.tick(_pressed[2]);
    }

void ButtonInput::update() {
        if (!_taskStarted) sample();
        if (_overflow.exchange(false, std::memory_order_acq_rel)) {
            Serial.println("[btn] queue overflow: discarding stale gesture");
            suppressUntilAllReleased();
            return;
        }
        Event event;
        // Bound dispatch work; slow callbacks must not starve the rest of loop.
        for (int count = 0; count < 16 && pop(event); ++count) {
            if (event.generation != _generation.load(std::memory_order_acquire)) continue;
            Kind kind = event.kind;
            if (kind == Press) {
                if (_wake && _wake()) { suppressUntilAllReleased(); return; }
                continue;
            }
            if (kind == LongDown || kind == LongUp) {
                if (_repeatEnabled) kind = kind == LongDown ? RepeatDown : RepeatUp;
            } else if ((kind == RepeatDown || kind == RepeatUp) && !_repeatEnabled) continue;
            if (_callbacks[kind]) {
                Serial.printf("[btn] dispatch %u\n", (unsigned)kind);
                _callbacks[kind]();
            }
        }
    }
