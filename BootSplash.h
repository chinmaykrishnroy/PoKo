#pragma once
#include <stdint.h>

class Arduino_GFX;
class Arduino_Canvas;

// Driven by existing setup milestones: no waits, timer, or background task.
class BootSplash {
public:
    BootSplash();
    ~BootSplash();
    BootSplash(const BootSplash&) = delete;
    BootSplash& operator=(const BootSplash&) = delete;
    bool begin(Arduino_GFX* display, uint32_t seed);
    void update(uint8_t progress);
    void finish();

private:
    struct Ray {
        float x, y, targetX, targetY, bend;
        uint16_t color;
    };
    Arduino_Canvas* _canvas;
    Ray _rays[6];
    int _progress;
    void glow(int x, int y, int radius, uint16_t color, int strength);
};
