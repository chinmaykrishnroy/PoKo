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
    static ButtonInput* _instance;

    void emit(Kind kind);
    bool pop(Event& event);
    static void resetButton(OneButton& button);
    void resetRecognizers();
    template<Kind kind> static void callback() { if (_instance) _instance->emit(kind); }
#ifdef ARDUINO_ARCH_ESP32
    static void task(void* context);
#endif
public:
    ButtonInput();
    void begin();

    void onPress(WakeCb cb);
    void setHoldRepeatEnabled(bool enabled);
    void onDown(SimpleCb cb);
    void onUp(SimpleCb cb);
    void onDownDouble(SimpleCb cb);
    void onUpDouble(SimpleCb cb);
    void onLongDown(SimpleCb cb);
    void onLongUp(SimpleCb cb);
    void onDownHolding(SimpleCb cb);
    void onUpHolding(SimpleCb cb);
    void onPwrClick(SimpleCb cb);
    // Register before begin() if a future product defines a power double action.
    void onPwrDouble(SimpleCb cb);
    void onPwrLong(SimpleCb cb);
    void onBothClick(SimpleCb cb);
    void onBothDouble(SimpleCb cb);
    void onBothLong(SimpleCb cb);
    void onBothVLong(SimpleCb cb);
    void onBothUltra(SimpleCb cb);
    void onLeft(SimpleCb cb);
    void onRight(SimpleCb cb);
    void onLeftDouble(SimpleCb cb);
    void onRightDouble(SimpleCb cb);
    void onLongLeft(SimpleCb cb);
    void onLongRight(SimpleCb cb);
    void onLeftHolding(SimpleCb cb);
    void onRightHolding(SimpleCb cb);

    void suppressUntilAllReleased();
    void reset();

    // Single producer: worker in firmware, explicitly stepped in host tests.
    void sample();

    void update();
};

