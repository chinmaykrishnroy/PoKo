#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
using String=std::string;
inline uint32_t testMillis=0;
inline uint32_t millis() { return testMillis; }
inline uint32_t micros() { return testMillis*1000; }
