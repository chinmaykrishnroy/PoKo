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
    void reset(uint32_t now) { _last = now; _offset = 0; }
    bool update(uint32_t now, uint16_t width) {
        if (width <= 120) { _offset = 0; _last = now; return false; }
        uint32_t steps = (now - _last) / STEP_MS;
        if (!steps) return false;
        _last += steps * STEP_MS;
        _offset = (_offset + steps) % (width + GAP);
        return true;
    }
    int offset() const { return _offset; }
    static uint32_t readingTime(uint16_t width) {
        return width <= 120 ? 2000 : (uint32_t(width) + GAP) * STEP_MS + 1000;
    }
};

