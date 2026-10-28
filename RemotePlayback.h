#pragma once
#include <cstdint>

// Borrowed strings are copied into app-owned storage before acknowledging.
struct RemotePlayback {
    const char* id = "";
    const char* title = "";
    const char* artist = "";
    uint32_t duration = 0;
    uint32_t position = 0;
    int index = 0;
    int total = 0;
};
