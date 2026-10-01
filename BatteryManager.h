#pragma once
#include <Arduino.h>

class BatteryManager {
public:
    BatteryManager() = default;

    void begin();
    void readNow();
    void update(uint32_t intervalMs = 10000);

    bool consumePluggedInEvent();
    bool isPresent() const;
    float getVoltage() const;
    int getPercentage() const;
    bool isCharging() const;
    bool isFull() const;
    bool isLow() const;
    bool isCritical() const;

private:
    struct BatteryLutEntry { float volts; int percent; };
    static const BatteryLutEntry BATTERY_LUT[];
    static constexpr size_t LUT_SIZE = 11;

    int voltageToPercent(float voltage) const;
    float sampleAdcVoltage();

    float _voltage = 0.0f;
    int _percentage = -1;
    bool _isCharging = false;
    bool _isFull = false;
    uint32_t _lastReadMs = 0;
    uint32_t _lowVoltageStartMs = 0;
    bool _isCritical = false;
    uint32_t _lastChgCheckMs = 0;
    uint8_t _chgFilterCount = 0;
    bool _justPluggedIn = false;
};
