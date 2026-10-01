#include "BatteryManager.h"
#include "PokoPins.h"
#include <math.h>

const BatteryManager::BatteryLutEntry BatteryManager::BATTERY_LUT[] = {
    {4.18f, 100}, {4.10f, 95}, {4.00f, 85}, {3.90f, 75}, {3.82f, 65},
    {3.75f, 50}, {3.70f, 35}, {3.60f, 20}, {3.50f, 10}, {3.40f, 5}, {3.25f, 0}
};

int BatteryManager::voltageToPercent(float voltage) const {
    if (voltage >= BATTERY_LUT[0].volts) return 100;
    if (voltage <= BATTERY_LUT[LUT_SIZE - 1].volts) return 0;
    for (size_t i = 0; i < LUT_SIZE - 1; ++i) {
        if (voltage <= BATTERY_LUT[i].volts && voltage >= BATTERY_LUT[i + 1].volts) {
            float spanV = BATTERY_LUT[i].volts - BATTERY_LUT[i + 1].volts;
            float diffV = voltage - BATTERY_LUT[i + 1].volts;
            int spanP = BATTERY_LUT[i].percent - BATTERY_LUT[i + 1].percent;
            return BATTERY_LUT[i + 1].percent + (int)lroundf((diffV / spanV) * spanP);
        }
    }
    return 0;
}

float BatteryManager::sampleAdcVoltage() {
    uint32_t samples[8];
    for (int i = 0; i < 8; ++i) {
        samples[i] = analogReadMilliVolts(POKO_PIN_BAT_ADC);
        delayMicroseconds(250);
    }
    for (int i = 1; i < 8; ++i) {
        uint32_t key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = key;
    }
    uint32_t avgMv = (samples[2] + samples[3] + samples[4] + samples[5]) / 4;
    return (float)avgMv * 3.0f / 1000.0f;
}

void BatteryManager::begin() {
    pinMode(POKO_PIN_CHARGING, INPUT_PULLUP);
    pinMode(POKO_PIN_BAT_ADC, INPUT);
    analogSetPinAttenuation(POKO_PIN_BAT_ADC, ADC_11db);
    _isCharging = digitalRead(POKO_PIN_CHARGING) == LOW;
    readNow();
}

void BatteryManager::readNow() {
    float rawV = sampleAdcVoltage();
    _voltage = _voltage <= 0.1f ? rawV : (_voltage * 0.8f) + (rawV * 0.2f);
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
    _isFull = !_isCharging && _voltage >= 4.15f;
    if (_voltage < 3.25f && !_isCharging) {
        if (_lowVoltageStartMs == 0) _lowVoltageStartMs = millis();
        else if (millis() - _lowVoltageStartMs > 15000) _isCritical = true;
    } else {
        _lowVoltageStartMs = 0;
        _isCritical = false;
    }
    _lastReadMs = millis();
}

void BatteryManager::update(uint32_t intervalMs) {
    uint32_t now = millis();
    if (now - _lastChgCheckMs >= 50) {
        _lastChgCheckMs = now;
        bool rawCharging = digitalRead(POKO_PIN_CHARGING) == LOW;
        if (rawCharging != _isCharging) {
            if (++_chgFilterCount >= 2) {
                bool wasCharging = _isCharging;
                _isCharging = rawCharging;
                _chgFilterCount = 0;
                _justPluggedIn = _justPluggedIn || (_isCharging && !wasCharging);
                Serial.printf("[bat] Charging state change: %s\n", _isCharging ? "CHARGING" : "DISCHARGING");
                readNow();
                return;
            }
        } else {
            _chgFilterCount = 0;
        }
    }
    if (now - _lastReadMs >= intervalMs) readNow();
}

bool BatteryManager::consumePluggedInEvent() {
    bool event = _justPluggedIn;
    _justPluggedIn = false;
    return event;
}

bool BatteryManager::isPresent() const { return _voltage >= 2.50f; }
float BatteryManager::getVoltage() const { return _voltage; }
int BatteryManager::getPercentage() const { return _percentage; }
bool BatteryManager::isCharging() const { return isPresent() && _isCharging; }
bool BatteryManager::isFull() const { return isPresent() && _isFull; }
bool BatteryManager::isLow() const { return isPresent() && _percentage <= 15 && !_isCharging; }
bool BatteryManager::isCritical() const { return isPresent() && _isCritical; }
