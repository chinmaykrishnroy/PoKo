#pragma once
#include <stdint.h>

class Arduino_GFX;
class Arduino_Canvas;

// Brief cold-boot animation. Setup advances it; finish completes its minimum
// visible duration while yielding and servicing the watchdog. No background task.
class BootSplash {
public:
    BootSplash();
    ~BootSplash();
    BootSplash(const BootSplash&) = delete;
    BootSplash& operator=(const BootSplash&) = delete;
    bool begin(Arduino_GFX* display, uint32_t seed);
    void update(uint8_t progress);
    void tick();
    void finish();

private:
    struct Ray {
        float x, y, targetX, targetY, bend;
        uint16_t color;
    };
    Arduino_Canvas* _canvas;
    Ray _rays[6];
    int _progress;
    uint32_t _startedMs;
    uint32_t _lastFrameMs;
    void glow(int x, int y, int radius, uint16_t color, int strength);
};
