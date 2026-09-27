#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"
#include "PixelEngine.h"

// ─────────────────────────────────────────────────────────────
//  PixelApp — On-device NeoPixel Ring Studio (128×128)
//  Features:
//    - Real-time Color Swatch & 8-LED ring preview diagram
//    - R, G, B color sliders with live feedback
//    - Target pixel selection (All 8 or individual LED 1..8)
//    - Animation preset selection (Spinner, Rainbow, Breathe, etc.)
//    - Music & SSync reactive lighting controls
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class PixelApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active    = false;
    bool     _dirty     = true;
    uint8_t  _selected  = 0;
    uint8_t  _scroll    = 0;
    uint32_t _lastDrawMs = 0;

    static constexpr uint8_t ITEM_COUNT   = 11;
    static constexpr uint8_t ROW_H        = 14;
    static constexpr uint8_t TOP_Y        = 42;
    static constexpr uint8_t ROWS_VISIBLE = 5;
    static constexpr uint8_t FOOTER_Y     = 114;

    const char* _items[ITEM_COUNT] = {
        "Mode",
        "Red",
        "Green",
        "Blue",
        "Target",
        "Bright",
        "Music Light",
        "Music FX",
        "SSync Light",
        "SSync FX",
        "Freq Resp"
    };

    void adjustScroll() {
        if (_selected < _scroll) {
            _scroll = _selected;
        } else if (_selected >= _scroll + ROWS_VISIBLE) {
            _scroll = _selected - ROWS_VISIBLE + 1;
        }
    }

    uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        // 1. Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(theme.accent, theme.headerBg);
        _canvas->setCursor(3, 11);
        _canvas->print("Pixels");

        // Header item counter
        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _selected + 1, ITEM_COUNT);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.muted, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 10);
        _canvas->print(countBuf);

        // 2. Visual Preview Swatch & 8-LED Ring Diagram (y=14..40)
        _canvas->fillRect(0, 14, 128, 27, theme.surface);
        _canvas->drawFastHLine(0, 40, 128, theme.line);
        _canvas->setTextWrap(false);

        uint8_t curR = pixelEngine.getR();
        uint8_t curG = pixelEngine.getG();
        uint8_t curB = pixelEngine.getB();
        uint16_t swatchCol = toRgb565(curR, curG, curB);

        // Color Swatch Box (x=5, y=17, w=22, h=19)
        _canvas->fillRoundRect(5, 17, 22, 19, 3, swatchCol);
        _canvas->drawRoundRect(5, 17, 22, 19, 3, theme.line);

        // Mini 8-LED Ring Visualizer (center cx=46, cy=26)
        const int cx = 46;
        const int cy = 26;
        uint8_t curMask = pixelEngine.getTargetMask();
        for (int a = 0; a < POKO_LED_COUNT; a++) {
            float angle = a * (6.2831853f / 8.0f) - 1.5707963f;
            int dx = cx + (int)(cosf(angle) * 8.5f + 0.5f);
            int dy = cy + (int)(sinf(angle) * 8.5f + 0.5f);

            bool inMask = (curMask & (1 << a)) != 0;
            if (inMask) {
                CRGB ledCol = pixelEngine.getPixelColor(a);
                uint16_t c565 = toRgb565(ledCol.r, ledCol.g, ledCol.b);
                _canvas->fillCircle(dx, dy, 2, c565);
                if (curMask != 0xFF) {
                    _canvas->drawCircle(dx, dy, 3, theme.accent);
                }
            } else {
                _canvas->drawCircle(dx, dy, 2, theme.line);
            }
        }

        // Color Hex text (x=68, y=24)
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.text, theme.surface);
        char hexBuf[10];
        snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", curR, curG, curB);
        _canvas->setCursor(68, 24);
        _canvas->print(hexBuf);

        // Target badge (x=68, y=34)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.muted, theme.surface);
        _canvas->setCursor(68, 34);
        _canvas->print(pixelEngine.getTargetMaskLabel());

        // 3. Scrollable Parameter Rows (y=41..113)
        _canvas->fillRect(0, TOP_Y, 128, FOOTER_Y - TOP_Y, theme.bg);
        _canvas->setFont(u8g2_font_profont10_mf);

        for (uint8_t i = 0; i < ROWS_VISIBLE; i++) {
            uint8_t itemIdx = _scroll + i;
            if (itemIdx >= ITEM_COUNT) break;

            int16_t rowY = TOP_Y + 1 + i * ROW_H;
            bool isSel = (itemIdx == _selected);

            if (isSel) {
                _canvas->fillRect(0, rowY - 1, 124, 13, theme.surface2);
                _canvas->drawFastHLine(0, rowY - 1, 124, theme.accent);
                _canvas->drawFastHLine(0, rowY + 11, 124, theme.accent);
            }

            _canvas->setTextColor(isSel ? theme.text : theme.muted, isSel ? theme.surface2 : theme.bg);
            _canvas->setCursor(3, rowY + 8);
            _canvas->print(_items[itemIdx]);

            // Right-aligned value string
            char valBuf[20] = "";
            uint16_t valCol = isSel ? theme.accent : theme.muted;

            switch (itemIdx) {
                case 0: { // Mode
                    switch (pixelEngine.getMode()) {
                        case PIXEL_MODE_OFF:     snprintf(valBuf, sizeof(valBuf), "Off"); break;
                        case PIXEL_MODE_SOLID:   snprintf(valBuf, sizeof(valBuf), "Solid"); break;
                        case PIXEL_MODE_SPINNER: snprintf(valBuf, sizeof(valBuf), "Spinner"); break;
                        case PIXEL_MODE_RAINBOW: snprintf(valBuf, sizeof(valBuf), "Rainbow"); break;
                        case PIXEL_MODE_BREATHE: snprintf(valBuf, sizeof(valBuf), "Breathe"); break;
                        case PIXEL_MODE_FIRE:    snprintf(valBuf, sizeof(valBuf), "Fire"); break;
                        default:                 snprintf(valBuf, sizeof(valBuf), "Spinner"); break;
                    }
                    break;
                }
                case 1: snprintf(valBuf, sizeof(valBuf), "%d", curR); valCol = 0xF800; break;
                case 2: snprintf(valBuf, sizeof(valBuf), "%d", curG); valCol = 0x07E0; break;
                case 3: snprintf(valBuf, sizeof(valBuf), "%d", curB); valCol = 0x001F; break;
                case 4: { // Target
                    snprintf(valBuf, sizeof(valBuf), "%s", pixelEngine.getTargetMaskLabel());
                    break;
                }
                case 5: snprintf(valBuf, sizeof(valBuf), "%d%%", (int)(pixelEngine.getBrightness() * 100 / 255)); break;
                case 6: snprintf(valBuf, sizeof(valBuf), pixelEngine.getMusicLightOn() ? "ON" : "OFF"); break;
                case 7: { // Music Effect
                    switch (pixelEngine.getMusicEffect()) {
                        case MUSIC_FX_AUTO:     snprintf(valBuf, sizeof(valBuf), "Auto (Art)"); break;
                        case MUSIC_FX_PROGRESS: snprintf(valBuf, sizeof(valBuf), "Progress"); break;
                        case MUSIC_FX_RED:      snprintf(valBuf, sizeof(valBuf), "Red"); break;
                        case MUSIC_FX_GREEN:    snprintf(valBuf, sizeof(valBuf), "Green"); break;
                        case MUSIC_FX_BLUE:     snprintf(valBuf, sizeof(valBuf), "Blue"); break;
                        case MUSIC_FX_CYAN:     snprintf(valBuf, sizeof(valBuf), "Cyan"); break;
                        case MUSIC_FX_PURPLE:   snprintf(valBuf, sizeof(valBuf), "Purple"); break;
                        case MUSIC_FX_AMBER:    snprintf(valBuf, sizeof(valBuf), "Amber"); break;
                        case MUSIC_FX_RAINBOW:  snprintf(valBuf, sizeof(valBuf), "Rainbow"); break;
                        default:                snprintf(valBuf, sizeof(valBuf), "Auto"); break;
                    }
                    break;
                }
                case 8: snprintf(valBuf, sizeof(valBuf), pixelEngine.getSSyncLightOn() ? "ON" : "OFF"); break;
                case 9: { // SSync Effect
                    switch (pixelEngine.getSSyncEffect()) {
                        case SSYNC_FX_VOL_HUE: snprintf(valBuf, sizeof(valBuf), "Vol Hue"); break;
                        case SSYNC_FX_RAINBOW: snprintf(valBuf, sizeof(valBuf), "Rainbow"); break;
                        case SSYNC_FX_CYAN:    snprintf(valBuf, sizeof(valBuf), "Cyan"); break;
                        case SSYNC_FX_MAGENTA: snprintf(valBuf, sizeof(valBuf), "Magenta"); break;
                        case SSYNC_FX_AMBER:   snprintf(valBuf, sizeof(valBuf), "Amber"); break;
                        default:               snprintf(valBuf, sizeof(valBuf), "Vol Hue"); break;
                    }
                    break;
                }
                case 10: { // Freq Resp
                    switch (pixelEngine.getFreqResponse()) {
                        case FREQ_RESP_LOW:  snprintf(valBuf, sizeof(valBuf), "Low"); break;
                        case FREQ_RESP_MID:  snprintf(valBuf, sizeof(valBuf), "Mid"); break;
                        case FREQ_RESP_HIGH: snprintf(valBuf, sizeof(valBuf), "High"); break;
                        case FREQ_RESP_ALL:  snprintf(valBuf, sizeof(valBuf), "All"); break;
                        default:             snprintf(valBuf, sizeof(valBuf), "Low"); break;
                    }
                    break;
                }
            }

            _canvas->setTextColor(valCol, isSel ? theme.surface2 : theme.bg);
            _canvas->getTextBounds(valBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(122 - w, rowY + 8);
            _canvas->print(valBuf);
        }

        // Scrollbar indicator
        if (ITEM_COUNT > ROWS_VISIBLE) {
            uint8_t barH = (ROWS_VISIBLE * (FOOTER_Y - TOP_Y)) / ITEM_COUNT;
            uint8_t barY = TOP_Y + (_scroll * (FOOTER_Y - TOP_Y - barH)) / (ITEM_COUNT - ROWS_VISIBLE);
            _canvas->drawFastVLine(126, TOP_Y, FOOTER_Y - TOP_Y, theme.line);
            _canvas->drawFastVLine(126, barY, barH, theme.accent);
        }

        // 4. Footer (y=114..127)
        _canvas->fillRect(0, FOOTER_Y, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, FOOTER_Y, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = "L/R:Nav  2R:Set  Hold:Adj";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

    void applyAction() {
        switch (_selected) {
            case 0: { // Cycle Mode
                uint8_t m = (uint8_t)pixelEngine.getMode();
                m = (m + 1) % PIXEL_MODE_COUNT;
                pixelEngine.setMode((PixelMode)m);
                break;
            }
            case 1: { // Red step (+25, wraps to 0)
                uint8_t r = pixelEngine.getR();
                r = (r >= 240) ? 0 : (r + 25);
                pixelEngine.setColor(r, pixelEngine.getG(), pixelEngine.getB());
                break;
            }
            case 2: { // Green step (+25, wraps to 0)
                uint8_t g = pixelEngine.getG();
                g = (g >= 240) ? 0 : (g + 25);
                pixelEngine.setColor(pixelEngine.getR(), g, pixelEngine.getB());
                break;
            }
            case 3: { // Blue step (+25, wraps to 0)
                uint8_t b = pixelEngine.getB();
                b = (b >= 240) ? 0 : (b + 25);
                pixelEngine.setColor(pixelEngine.getR(), pixelEngine.getG(), b);
                break;
            }
            case 4: { // Cycle Target: All 8 -> LED 1..8 -> All 8
                uint8_t mask = pixelEngine.getTargetMask();
                if (mask == 0xFF) {
                    pixelEngine.setTargetMask(1 << 0); // LED 1
                } else if ((mask & (mask - 1)) == 0 && mask != 0) {
                    uint8_t curBit = 0;
                    for (uint8_t k = 0; k < 8; k++) {
                        if (mask == (1 << k)) { curBit = k; break; }
                    }
                    if (curBit >= 7) {
                        pixelEngine.setTargetMask(0xFF); // Wrap to All 8
                    } else {
                        pixelEngine.setTargetMask(1 << (curBit + 1));
                    }
                } else {
                    pixelEngine.setTargetMask(0xFF); // Reset from WEB to All 8
                }
                break;
            }
            case 5: { // Cycle Brightness: 25 -> 60 -> 120 -> 200 -> 255 -> 25
                uint8_t br = pixelEngine.getBrightness();
                if (br <= 30)       br = 60;
                else if (br <= 70)  br = 120;
                else if (br <= 150) br = 200;
                else if (br <= 220) br = 255;
                else                br = 25;
                pixelEngine.setBrightness(br);
                break;
            }
            case 6: { // Toggle Music Light
                pixelEngine.setMusicLightOn(!pixelEngine.getMusicLightOn());
                break;
            }
            case 7: { // Cycle Music Effect Preset
                uint8_t fx = (uint8_t)pixelEngine.getMusicEffect();
                fx = (fx + 1) % MUSIC_FX_COUNT;
                pixelEngine.setMusicEffect((MusicEffectPreset)fx);
                break;
            }
            case 8: { // Toggle SSync Light
                pixelEngine.setSSyncLightOn(!pixelEngine.getSSyncLightOn());
                break;
            }
            case 9: { // Cycle SSync Effect Preset
                uint8_t fx = (uint8_t)pixelEngine.getSSyncEffect();
                fx = (fx + 1) % SSYNC_FX_COUNT;
                pixelEngine.setSSyncEffect((SSyncEffectPreset)fx);
                break;
            }
            case 10: { // Cycle Freq Response
                uint8_t fr = (uint8_t)pixelEngine.getFreqResponse();
                fr = (fr + 1) % FREQ_RESP_COUNT;
                pixelEngine.setFreqResponse((FreqResponse)fr);
                break;
            }
        }
        pixelEngine.saveToPreferences(prefs);
        _dirty = true;
    }

    void adjustCurrentValue(int delta) {
        bool changed = false;
        switch (_selected) {
            case 1: { // Red
                int cur = (int)pixelEngine.getR() + delta;
                pixelEngine.setColor((uint8_t)constrain(cur, 0, 255), pixelEngine.getG(), pixelEngine.getB());
                changed = true;
                break;
            }
            case 2: { // Green
                int cur = (int)pixelEngine.getG() + delta;
                pixelEngine.setColor(pixelEngine.getR(), (uint8_t)constrain(cur, 0, 255), pixelEngine.getB());
                changed = true;
                break;
            }
            case 3: { // Blue
                int cur = (int)pixelEngine.getB() + delta;
                pixelEngine.setColor(pixelEngine.getR(), pixelEngine.getG(), (uint8_t)constrain(cur, 0, 255));
                changed = true;
                break;
            }
            case 5: { // Brightness
                int cur = (int)pixelEngine.getBrightness() + delta;
                pixelEngine.setBrightness((uint8_t)constrain(cur, 1, 255));
                changed = true;
                break;
            }
        }
        if (changed) {
            pixelEngine.saveToPreferences(prefs);
            _dirty = true;
        }
    }

public:
    PixelApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active   = true;
        _selected = 0;
        _scroll   = 0;
        _dirty    = true;
        begin();
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        _selected = (_selected == 0) ? (ITEM_COUNT - 1) : (_selected - 1);
        adjustScroll();
        _dirty = true;
    }

    void onRight() {
        _selected = (_selected + 1) % ITEM_COUNT;
        adjustScroll();
        _dirty = true;
    }

    void onHoldingLeft() {
        adjustCurrentValue(-6);
    }

    void onHoldingRight() {
        adjustCurrentValue(6);
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        applyAction();
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        // Redraw preview every 80ms for lively LED visualizer
        if (now - _lastDrawMs >= 80) {
            _lastDrawMs = now;
            _dirty = true;
        }
        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
};
