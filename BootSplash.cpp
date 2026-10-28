#include "BootSplash.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include <math.h>
#include <new>

namespace {
uint32_t randomStep(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void wordmark(Arduino_GFX& gfx) {
    gfx.setTextWrap(false);
    gfx.setTextSize(1);
    gfx.setFont(u8g2_font_helvB24_tf);
    gfx.setTextColor(0xffff);
    int16_t x, y;
    uint16_t w, h;
    gfx.getTextBounds("PoKo", 0, 0, &x, &y, &w, &h);
    gfx.setCursor((128 - (int)w) / 2 - x, (128 - (int)h) / 2 - y);
    gfx.print("PoKo");
}
}

BootSplash::BootSplash() : _canvas(nullptr), _progress(-1), _startedMs(0), _lastFrameMs(0) {}
BootSplash::~BootSplash() { delete _canvas; }

bool BootSplash::begin(Arduino_GFX* display, uint32_t seed) {
    delete _canvas;
    _canvas = nullptr;
    _progress = -1;
    _startedMs = millis();
    _lastFrameMs = 0;
    if (!display) return false;
    _canvas = new (std::nothrow) Arduino_Canvas(128, 128, display);
    if (!_canvas || !_canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        delete _canvas;
        _canvas = nullptr;
        display->fillScreen(0);
        wordmark(*display);
        return true; // Still draw a complete white logo if memory is tight.
    }
    uint32_t state = seed ? seed : 0x504f4b4fU;
    const uint16_t palette[] = {0x07ff, 0xf81f, 0xfd20, 0x781f, 0x07ef, 0xf96d};
    unsigned rotation = randomStep(state) % 6;
    for (unsigned i = 0; i < 6; ++i) {
        float angle = (randomStep(state) & 4095) * (6.2831853f / 4096);
        _rays[i] = {64 + cosf(angle) * 92, 64 + sinf(angle) * 92,
                    28.0f + randomStep(state) % 73, 56.0f + randomStep(state) % 17,
                    (float)((int)(randomStep(state) % 41) - 20), palette[(i + rotation) % 6]};
    }
    update(0);
    return true;
}

void BootSplash::glow(int x, int y, int radius, uint16_t color, int strength) {
    uint16_t* pixels = _canvas->getFramebuffer();
    int rr = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        int py = y + dy;
        if (py < 0 || py >= 128) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = x + dx, distance = dx * dx + dy * dy;
            if (px < 0 || px >= 128 || distance >= rr) continue;
            int alpha = (rr - distance) * strength / rr;
            uint16_t& pixel = pixels[py * 128 + px];
            int r = (pixel >> 11) + ((color >> 11) * alpha / 255);
            int g = ((pixel >> 5) & 63) + (((color >> 5) & 63) * alpha / 255);
            int b = (pixel & 31) + ((color & 31) * alpha / 255);
            pixel = ((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b);
        }
    }
}

void BootSplash::update(uint8_t progress) {
    if (!_canvas || progress <= _progress) return;
    _progress = progress;
    _canvas->fillScreen(0);
    if (progress > 0 && progress < 255) {
        float t = progress / 255.0f;
        float fade = t < 0.75f ? 1.0f : (1.0f - t) * 4.0f;
        for (const Ray& ray : _rays) {
            for (int tail = 3; tail >= 0; --tail) {
                float p = t - tail * 0.065f;
                if (p < 0) continue;
                float ease = 1 - (1 - p) * (1 - p) * (1 - p);
                float curve = sinf(p * 3.1415927f) * ray.bend;
                int x = (int)(ray.x + (ray.targetX - ray.x) * ease + curve);
                int y = (int)(ray.y + (ray.targetY - ray.y) * ease - curve * 0.5f);
                glow(x, y, 13 + (int)(p * 6), ray.color, (int)(fade * (120 - tail * 24)));
            }
        }
    }
    wordmark(*_canvas); // Text stays white above the additive color fusion.
    _canvas->flush();
}

void BootSplash::tick() {
    if (!_canvas) return;
    uint32_t elapsed = millis() - _startedMs;
    if (elapsed - _lastFrameMs < 33) return;
    _lastFrameMs = elapsed;
    // White introduction, slower flowing color, then settle back to white.
    uint32_t progress = elapsed <= 120 ? 0 : (elapsed - 120) * 255 / 850;
    update(progress > 255 ? 255 : (uint8_t)progress);
}

void BootSplash::finish() {
    while (_canvas && millis() - _startedMs < 1100) {
        tick();
        esp_task_wdt_reset();
        delay(8);
    }
    update(255);
    delete _canvas;
    _canvas = nullptr;
}
