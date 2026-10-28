#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

constexpr int GFX_SKIP_OUTPUT_BEGIN = -2;
inline bool failCanvasBegin = false;
inline int liveCanvases = 0;

class Arduino_GFX {
public:
    std::vector<uint16_t> pixels = std::vector<uint16_t>(128 * 128);
    std::string text;
    uint16_t color = 0;
    int beginCalls = 0, flushes = 0, cursorX = 0, cursorY = 0;
    virtual ~Arduino_GFX() = default;
    virtual bool begin(int = 0) { ++beginCalls; return true; }
    void fillScreen(uint16_t c) { std::fill(pixels.begin(), pixels.end(), c); }
    void setTextWrap(bool) {}
    void setTextSize(int) {}
    void setFont(const uint8_t*) {}
    void setTextColor(uint16_t c) { color = c; }
    void getTextBounds(const char*, int, int, int16_t* x, int16_t* y, uint16_t* w, uint16_t* h) {
        *x = 0; *y = -25; *w = 90; *h = 30;
    }
    void setCursor(int x, int y) { cursorX = x; cursorY = y; }
    void print(const char* value) {
        text = value;
        // Stand in for glyph rasterization; test production color compositing.
        for (int y = cursorY - 25; y < cursorY + 5; ++y)
            for (int x = cursorX; x < cursorX + 90; ++x)
                if (x >= 0 && x < 128 && y >= 0 && y < 128 && (x % 20) < 6)
                    pixels[y * 128 + x] = color;
    }
};

class Arduino_Canvas : public Arduino_GFX {
    Arduino_GFX* output;
public:
    Arduino_Canvas(int, int, Arduino_GFX* gfx) : output(gfx) { ++liveCanvases; }
    ~Arduino_Canvas() { --liveCanvases; }
    bool begin(int speed = 0) override {
        if (speed != GFX_SKIP_OUTPUT_BEGIN) output->begin(speed);
        return !failCanvasBegin;
    }
    uint16_t* getFramebuffer() { return pixels.data(); }
    void flush() { output->pixels = pixels; output->text = text; output->color = color; ++output->flushes; }
};
