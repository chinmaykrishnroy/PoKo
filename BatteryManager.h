#pragma once
#include <Arduino.h>
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  BatteryManager — Voltage, percentage, and charging monitor
//  Hardware: Waveshare ESP32-S3-LCD-0.85
//  BAT_ADC: GPIO 1 (1:2 divider -> multiplier = 3.0)
//  CHARGING: GPIO 3 (Active LOW, pullup enabled)
// ─────────────────────────────────────────────────────────────

class BatteryManager {
private:
    float    _voltage           = 0.0f;
    int      _percentage        = -1;
    bool     _isCharging        = false;
    bool     _isFull            = false;
    uint32_t _lastReadMs        = 0;
    uint32_t _lowVoltageStartMs = 0;
    bool     _isCritical        = false;

    // Piecewise single-cell LiPo/Li-ion discharge table: {voltage, percent}
    struct BatteryLutEntry {
        float volts;
        int   percent;
    };

    static constexpr BatteryLutEntry BATTERY_LUT[] = {
        { 4.18f, 100 },
        { 4.10f,  95 },
        { 4.00f,  85 },
        { 3.90f,  75 },
        { 3.82f,  65 },
        { 3.75f,  50 },
        { 3.70f,  35 },
        { 3.60f,  20 },
        { 3.50f,  10 },
        { 3.40f,   5 },
        { 3.25f,   0 }
    };
    static constexpr size_t LUT_SIZE = sizeof(BATTERY_LUT) / sizeof(BATTERY_LUT[0]);

    int voltageToPercent(float v) const {
        if (v >= BATTERY_LUT[0].volts) return 100;
        if (v <= BATTERY_LUT[LUT_SIZE - 1].volts) return 0;

        for (size_t i = 0; i < LUT_SIZE - 1; i++) {
            if (v <= BATTERY_LUT[i].volts && v >= BATTERY_LUT[i + 1].volts) {
                float spanV = BATTERY_LUT[i].volts - BATTERY_LUT[i + 1].volts;
                float diffV = v - BATTERY_LUT[i + 1].volts;
                int spanP = BATTERY_LUT[i].percent - BATTERY_LUT[i + 1].percent;
                return BATTERY_LUT[i + 1].percent + (int)lroundf((diffV / spanV) * spanP);
            }
        }
        return 0;
    }

    float sampleAdcVoltage() {
        // Collect 8 ADC samples for noise rejection
        uint32_t samples[8];
        for (int i = 0; i < 8; i++) {
            samples[i] = analogReadMilliVolts(POKO_PIN_BAT_ADC);
            delayMicroseconds(250);
        }

        // Simple insertion sort to find median
        for (int i = 1; i < 8; i++) {
            uint32_t key = samples[i];
            int j = i - 1;
            while (j >= 0 && samples[j] > key) {
                samples[j + 1] = samples[j];
                j--;
            }
            samples[j + 1] = key;
        }

        // Average the 4 middle samples (discard 2 lowest and 2 highest)
        uint32_t avgMv = (samples[2] + samples[3] + samples[4] + samples[5]) / 4;

        // Multiply by 3.0 (verified 1:2 divider from Waveshare factory BSP)
        return (float)avgMv * 3.0f / 1000.0f;
    }

    uint32_t _lastChgCheckMs    = 0;
    uint8_t  _chgFilterCount    = 0;
    bool     _justPluggedIn     = false;

public:
    BatteryManager() {}

    void begin() {
        pinMode(POKO_PIN_CHARGING, INPUT_PULLUP);
        pinMode(POKO_PIN_BAT_ADC, INPUT);
        analogSetPinAttenuation(POKO_PIN_BAT_ADC, ADC_11db);

        // Immediate first read
        readNow();
    }

    void readNow() {
        float rawV = sampleAdcVoltage();
        if (_voltage <= 0.1f) {
            _voltage = rawV;
        } else {
            // Exponential moving average: 80% previous, 20% new
            _voltage = (_voltage * 0.8f) + (rawV * 0.2f);
        }

        if (!isPresent()) {
            _percentage = -1;
            _isCharging = false;
            _isFull = false;
            _isCritical = false;
            _lowVoltageStartMs = 0;
            _lastReadMs = millis();
            return;
        }

        _percentage = voltageToPercent(_voltage);

        // Charging status: pin is active LOW
        bool rawCharging = (digitalRead(POKO_PIN_CHARGING) == LOW);
        _isCharging = rawCharging;
        _isFull = (!rawCharging && _voltage >= 4.15f);

        // Check sustained critical low voltage (<3.25V sustained for 15 seconds)
        if (_voltage < 3.25f && !_isCharging) {
            if (_lowVoltageStartMs == 0) {
                _lowVoltageStartMs = millis();
            } else if (millis() - _lowVoltageStartMs > 15000) {
                _isCritical = true;
            }
        } else {
            _lowVoltageStartMs = 0;
            _isCritical = false;
        }

        _lastReadMs = millis();
    }

    void update(uint32_t intervalMs = 10000) {
        uint32_t now = millis();

        // 1. Fast charging pin poll (every 50ms) -> Instant plug/unplug detection!
        if (now - _lastChgCheckMs >= 50) {
            _lastChgCheckMs = now;
            bool rawChg = (digitalRead(POKO_PIN_CHARGING) == LOW);
            if (rawChg != _isCharging) {
                _chgFilterCount++;
                if (_chgFilterCount >= 2) { // 2 consecutive samples (100ms debounce)
                    bool wasCharging = _isCharging;
                    _isCharging = rawChg;
                    _chgFilterCount = 0;
                    if (_isCharging && !wasCharging) {
                        _justPluggedIn = true;
                    }
                    Serial.printf("[bat] Instant charging state change: %s\n", _isCharging ? "CHARGING" : "DISCHARGING");
                    readNow();
                    return;
                }
            } else {
                _chgFilterCount = 0;
            }
        }

        // 2. Periodic ADC voltage read
        if (now - _lastReadMs >= intervalMs) {
            readNow();
        }
    }

    bool  consumePluggedInEvent() {
        if (_justPluggedIn) {
            _justPluggedIn = false;
            return true;
        }
        return false;
    }

    bool  isPresent() const         { return _voltage >= 2.50f; }
    float getVoltage() const        { return _voltage; }
    int   getPercentage() const     { return _percentage; }
    bool  isCharging() const        { return isPresent() && _isCharging; }
    bool  isFull() const            { return isPresent() && _isFull; }
    bool  isLow() const             { return isPresent() && _percentage <= 15 && !_isCharging; }
    bool  isCritical() const        { return isPresent() && _isCritical; }
};
