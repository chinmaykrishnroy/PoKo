#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include <atomic>
#include "PokoPins.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rtc_io.h>
#endif

// GPIO sampling and OneButton run on one worker. Only update(), on the Arduino
// loop, calls application callbacks. No UI, I2S, network or prefs on the worker.
class ButtonInput {
public:
    using SimpleCb = void (*)();
    using WakeCb = bool (*)(); // true means consume the entire wake gesture
    static constexpr uint16_t BUTTON_CLICK_MS = 300;
    static constexpr uint16_t BUTTON_PRESS_MS = 700;
    static constexpr uint16_t BUTTON_DEBOUNCE_MS = 25;
    static constexpr uint16_t PWR_CLICK_MS = 300;
    static constexpr uint16_t PWR_PRESS_MS = 2000;
    static constexpr uint32_t DUAL_CLICK_MS = 450;

private:
    enum Kind : uint8_t { Press, Down, Up, DownDouble, UpDouble, LongDown,
        LongUp, RepeatDown, RepeatUp, Power, PowerDouble, PowerLong,
        Both, BothDouble, BothLong, BothVLong, BothUltra };
    struct Event { Kind kind; uint32_t generation; };
    static constexpr uint32_t QUEUE_SIZE = 64;
    Event _events[QUEUE_SIZE];
    std::atomic<uint32_t> _head{0}, _tail{0}, _generation{0};
    std::atomic<bool> _overflow{false};
    uint32_t _sampleGeneration = 0;
    OneButton _btnDown, _btnUp, _btnPwr;
    SimpleCb _callbacks[17] = {};
    WakeCb _wake = nullptr;
    bool _repeatEnabled = false; // main-loop only
    bool _taskStarted = false;
    bool _raw[3] = {}, _pressed[3] = {};
    uint32_t _changedAt[3] = {};
    bool _suppressed = false, _releaseTiming = false;
    uint32_t _releasedAt = 0;
    bool _combo = false, _comboReleasing = false;
    uint32_t _comboStart = 0, _dualReleased = 0;
    uint8_t _dualClicks = 0;
    static inline ButtonInput* _instance = nullptr;

    void emit(Kind kind) {
        uint32_t head = _head.load(std::memory_order_relaxed);
        uint32_t next = (head + 1) % QUEUE_SIZE;
        if (next == _tail.load(std::memory_order_acquire)) {
            _overflow.store(true, std::memory_order_release);
            return;
        }
        _events[head] = {kind, _sampleGeneration};
        _head.store(next, std::memory_order_release);
    }
    bool pop(Event& event) {
        uint32_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == _head.load(std::memory_order_acquire)) return false;
        event = _events[tail];
        _tail.store((tail + 1) % QUEUE_SIZE, std::memory_order_release);
        return true;
    }
    static void resetButton(OneButton& button) {
        // OneButton::reset() leaves its internal debounced level unchanged.
        // Prime it inactive before resuming after a consumed hold/chord.
        button.reset();
        button.tick(false);
        button.tick(false);
        button.reset();
    }
    void resetRecognizers() {
        resetButton(_btnDown); resetButton(_btnUp); resetButton(_btnPwr);
        _combo = _comboReleasing = false;
        _dualClicks = 0;
    }
    template<Kind kind> static void callback() { if (_instance) _instance->emit(kind); }
#ifdef ARDUINO_ARCH_ESP32
    static void task(void* context) {
        auto* self = static_cast<ButtonInput*>(context);
        TickType_t last = xTaskGetTickCount();
        for (;;) {
            self->sample();
            vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
        }
    }
#endif
public:
    ButtonInput() { _instance = this; }
    void begin() {
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

    void onPress(WakeCb cb) { _wake = cb; }
    void setHoldRepeatEnabled(bool enabled) { _repeatEnabled = enabled; }
    void onDown(SimpleCb cb) { _callbacks[Down] = cb; }
    void onUp(SimpleCb cb) { _callbacks[Up] = cb; }
    void onDownDouble(SimpleCb cb) { _callbacks[DownDouble] = cb; }
    void onUpDouble(SimpleCb cb) { _callbacks[UpDouble] = cb; }
    void onLongDown(SimpleCb cb) { _callbacks[LongDown] = cb; }
    void onLongUp(SimpleCb cb) { _callbacks[LongUp] = cb; }
    void onDownHolding(SimpleCb cb) { _callbacks[RepeatDown] = cb; }
    void onUpHolding(SimpleCb cb) { _callbacks[RepeatUp] = cb; }
    void onPwrClick(SimpleCb cb) { _callbacks[Power] = cb; }
    // Register before begin() if a future product defines a power double action.
    void onPwrDouble(SimpleCb cb) { _callbacks[PowerDouble] = cb; }
    void onPwrLong(SimpleCb cb) { _callbacks[PowerLong] = cb; }
    void onBothClick(SimpleCb cb) { _callbacks[Both] = cb; }
    void onBothDouble(SimpleCb cb) { _callbacks[BothDouble] = cb; }
    void onBothLong(SimpleCb cb) { _callbacks[BothLong] = cb; }
    void onBothVLong(SimpleCb cb) { _callbacks[BothVLong] = cb; }
    void onBothUltra(SimpleCb cb) { _callbacks[BothUltra] = cb; }
    void onLeft(SimpleCb cb) { onDown(cb); }
    void onRight(SimpleCb cb) { onUp(cb); }
    void onLeftDouble(SimpleCb cb) { onDownDouble(cb); }
    void onRightDouble(SimpleCb cb) { onUpDouble(cb); }
    void onLongLeft(SimpleCb cb) { onLongDown(cb); }
    void onLongRight(SimpleCb cb) { onLongUp(cb); }
    void onLeftHolding(SimpleCb cb) { onDownHolding(cb); }
    void onRightHolding(SimpleCb cb) { onUpHolding(cb); }

    void suppressUntilAllReleased() {
        _generation.fetch_add(1, std::memory_order_acq_rel);
        // A concurrent producer can still enqueue an old-generation event; the
        // consumer rejects it below. Only the worker resets the recognizers.
        _tail.store(_head.load(std::memory_order_acquire), std::memory_order_release);
    }
    void reset() { suppressUntilAllReleased(); }

    // Single producer: worker in firmware, explicitly stepped in host tests.
    void sample() {
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

    void update() {
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
};

