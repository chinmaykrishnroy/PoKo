#pragma once
#include <stdint.h>

// Same continuous 1px/40ms ticker and 32px gap as the Music app.
// Width comes from the font renderer, so short labels remain stationary.
class TitleMarquee {
    uint32_t _last = 0;
    uint16_t _offset = 0;
public:
    static constexpr uint32_t STEP_MS = 40;
    static constexpr uint16_t GAP = 32;
    void reset(uint32_t now);
    bool update(uint32_t now, uint16_t width);
    int offset() const;
    static uint32_t readingTime(uint16_t width);
};

