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
extern uint32_t testMillis;
extern int testPins[64];
extern int testPinSetups;
inline unsigned long millis() { return testMillis; }
inline int digitalRead(int pin) { return testPins[pin]; }
inline void pinMode(int, int) { ++testPinSetups; }
struct TestSerial {
    void println(const char*) {}
    template<class... T> void printf(const char*, T...) {}
};
inline TestSerial Serial;

