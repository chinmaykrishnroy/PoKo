#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
using std::max;
using std::min;
#define LOW 0
#define HIGH 1
#define INPUT 0
#define INPUT_PULLUP 2
#define ADC_11db 0
extern uint32_t testMillis;
extern int testPins[64];
extern int testPinSetups;
extern uint32_t testAdcMilliVolts;
inline unsigned long millis() { return testMillis; }
inline int digitalRead(int pin) { return testPins[pin]; }
inline void pinMode(int, int) { ++testPinSetups; }
inline uint32_t analogReadMilliVolts(int) { return testAdcMilliVolts; }
inline void analogSetPinAttenuation(int, int) {}
inline void delayMicroseconds(unsigned int) {}
struct TestSerial {
    void println(const char*) {}
    template<class... T> void printf(const char*, T...) {}
};
inline TestSerial Serial;

