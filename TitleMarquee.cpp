#include "TitleMarquee.h"

void TitleMarquee::reset(uint32_t now) {
    _last = now;
    _offset = 0;
}

bool TitleMarquee::update(uint32_t now, uint16_t width) {
    if (width <= 120) {
        _offset = 0;
        _last = now;
        return false;
    }
    uint32_t steps = (now - _last) / STEP_MS;
    if (!steps) return false;
    _last += steps * STEP_MS;
    _offset = (_offset + steps) % (width + GAP);
    return true;
}

int TitleMarquee::offset() const { return _offset; }

uint32_t TitleMarquee::readingTime(uint16_t width) {
    return width <= 120 ? 2000 : (uint32_t(width) + GAP) * STEP_MS + 1000;
}
