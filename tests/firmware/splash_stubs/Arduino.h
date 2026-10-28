#pragma once
#include <cstdint>
inline uint32_t splashClock=0;
inline uint32_t millis() { return splashClock; }
inline void delay(uint32_t ms) { splashClock+=ms; }
