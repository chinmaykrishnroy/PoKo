#include "AudioActivity.h"

bool pcm16HasAudibleSignal(const uint8_t* data, size_t bytes, int16_t threshold) {
    if (!data || bytes < 2) return false;
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        int16_t sample = static_cast<int16_t>(
            static_cast<uint16_t>(data[i]) |
            (static_cast<uint16_t>(data[i + 1]) << 8));
        int32_t magnitude = sample < 0 ? -static_cast<int32_t>(sample)
                                      : static_cast<int32_t>(sample);
        if (magnitude > threshold) return true;
    }
    return false;
}
