#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "PokoPins.h"

// ─────────────────────────────────────────────────────────────
//  PixelEngine — Central WS2812B 8-LED Ring Lighting Engine
//  Features:
//    - Standalone ambient modes (Spinner, Solid, Rainbow, Breathe, Fire, Off)
//    - Automatic Music Player Reactivity (Dual-Color Wave, Song Progress, Presets)
//    - Automatic SSync Snapcast Reactivity (Volume-Adaptive Hue & Waveform Pulse)
//    - Frequency Response Filter: Low (Bass/Kick), Mid, High, All
//    - Real-time Web OTA & ArduinoOTA Emerald Progress Ring & Error Flashing
//    - Perceptually distinct 2-color extraction from album art
//    - Preferences persistence across reboots
// ─────────────────────────────────────────────────────────────

enum PixelMode : uint8_t {
    PIXEL_MODE_OFF = 0,
    PIXEL_MODE_SOLID,        // Static color on target pixel(s)
    PIXEL_MODE_SPINNER,      // Light moving in ring with fading tail
    PIXEL_MODE_RAINBOW,      // Rotating rainbow spectrum
    PIXEL_MODE_BREATHE,      // Soft sine-wave breathing
    PIXEL_MODE_FIRE,         // Organic warm flame flicker
    PIXEL_MODE_COUNT
};

enum MusicEffectPreset : uint8_t {
    MUSIC_FX_AUTO = 0,       // Dual-color dynamic wave from album art
    MUSIC_FX_PROGRESS,       // Playback progress fill (secondary track -> primary progress)
    MUSIC_FX_RED,            // Crimson & Coral
    MUSIC_FX_GREEN,          // Emerald & Mint
    MUSIC_FX_BLUE,           // Deep Blue & Azure
    MUSIC_FX_CYAN,           // Cyan & Neon Blue
    MUSIC_FX_PURPLE,         // Violet & Magenta
    MUSIC_FX_AMBER,          // Gold & Flame
    MUSIC_FX_RAINBOW,        // Full dynamic spectrum
    MUSIC_FX_COUNT
};

enum SSyncEffectPreset : uint8_t {
    SSYNC_FX_VOL_HUE = 0,    // Volume-adaptive: Green (low) -> Amber (mid) -> Red (loud)
    SSYNC_FX_RAINBOW,        // Moving rainbow wave
    SSYNC_FX_CYAN,           // Neon Cyan beat pulse
    SSYNC_FX_MAGENTA,        // Vivid Magenta beat pulse
    SSYNC_FX_AMBER,          // Warm Amber beat pulse
    SSYNC_FX_COUNT
};

enum FreqResponse : uint8_t {
    FREQ_RESP_LOW = 0,       // Bass / Kick drum (< 250 Hz) - best for rhythm
    FREQ_RESP_MID,           // Vocals & Melody (250 Hz - 3 kHz)
    FREQ_RESP_HIGH,          // Cymbals & Treble (> 3 kHz)
    FREQ_RESP_ALL,           // Full spectrum raw peak
    FREQ_RESP_COUNT
};

class PixelEngine {
private:
    CRGB _leds[POKO_LED_COUNT];
    bool _initialized = false;

    // Ambient settings
    PixelMode _mode = PIXEL_MODE_SPINNER;
    uint8_t   _r = 0;
    uint8_t   _g = 200;
    uint8_t   _b = 255;
    uint8_t   _targetPixel = 8; // 8 = All 8 LEDs, 0..7 = single LED, 255 = custom WEB mask
    uint8_t   _targetMask = 0xFF; // Bitmask of enabled LEDs (bits 0..7)
    uint8_t   _brightness = 40; // 0..255
    uint16_t  _speedMs = 70;

    // Music reactive settings
    bool              _musicLightOn = true;
    MusicEffectPreset _musicEffect = MUSIC_FX_AUTO;
    float             _songProgress = 0.0f; // 0.0 to 1.0

    // SSync reactive settings
    bool              _ssyncLightOn = true;
    SSyncEffectPreset _ssyncEffect = SSYNC_FX_VOL_HUE;

    // Audio Frequency Response Filter
    FreqResponse _freqResp = FREQ_RESP_LOW;
    float _lp1 = 0.0f, _lp2 = 0.0f;
    float _hp = 0.0f, _prevIn = 0.0f;

    // Album art 2-color extraction
    CRGB _artColor1 = CRGB(0x16, 0x8B, 0xFF); // Vibrant Azure Blue
    CRGB _artColor2 = CRGB(0xFF, 0x55, 0x00); // Vibrant Sunset Orange

    // Color histogram for extraction (64 bins: 4x4x4)
    uint16_t _colorBins[64];
    uint32_t _binSumR[64];
    uint32_t _binSumG[64];
    uint32_t _binSumB[64];
    bool     _extracting = false;

    // Real-time audio reactive variables
    volatile float _audioLevel = 0.0f;  // Instantaneous smoothed audio envelope (0.0 .. 1.0)
    int            _effectiveVolume = 75; // 0..100 (master * app / 100)

    // OTA Visualizer state
    bool     _otaActive = false;
    bool     _otaErrorActive = false;
    uint32_t _otaErrorStartedAt = 0;
    uint32_t _lastOtaShowAt = 0;

    // Animation state
    uint32_t _lastFrameMs = 0;
    float    _spinnerPos = 0.0f;

    // Fast perceptual color distance (Redmean formula)
    static float colorDistance(CRGB c1, CRGB c2) {
        long rmean = ((long)c1.r + (long)c2.r) / 2;
        long r = (long)c1.r - (long)c2.r;
        long g = (long)c1.g - (long)c2.g;
        long b = (long)c1.b - (long)c2.b;
        return sqrtf((float)((((512 + rmean) * r * r) >> 8) + 4 * g * g + (((767 - rmean) * b * b) >> 8)));
    }

public:
    PixelEngine() {
        memset(_colorBins, 0, sizeof(_colorBins));
        memset(_binSumR, 0, sizeof(_binSumR));
        memset(_binSumG, 0, sizeof(_binSumG));
        memset(_binSumB, 0, sizeof(_binSumB));
    }

    void begin() {
        if (_initialized) return;
        FastLED.addLeds<WS2812B, POKO_PIN_LED_DATA, RGB>(_leds, POKO_LED_COUNT);
        FastLED.setBrightness(_brightness);
        fill_solid(_leds, POKO_LED_COUNT, CRGB::Black);
        FastLED.show();
        _initialized = true;
    }

    void loadFromPreferences(Preferences& prefs) {
        _mode = (PixelMode)constrain(prefs.getInt("px_mode", (int)PIXEL_MODE_SPINNER), 0, (int)PIXEL_MODE_COUNT - 1);
        _r = (uint8_t)prefs.getInt("px_r", 0);
        _g = (uint8_t)prefs.getInt("px_g", 200);
        _b = (uint8_t)prefs.getInt("px_b", 255);
        _targetPixel = (uint8_t)constrain(prefs.getInt("px_target", 8), 0, 255);
        _targetMask = (uint8_t)prefs.getInt("px_mask", _targetPixel == 8 ? 0xFF : (_targetPixel < 8 ? (1 << _targetPixel) : 0xFF));
        _brightness = (uint8_t)constrain(prefs.getInt("px_bright", 40), 1, 255);
        _speedMs = (uint16_t)constrain(prefs.getInt("px_speed", 70), 20, 2000);
        _musicLightOn = prefs.getBool("px_mus_on", true);
        _musicEffect = (MusicEffectPreset)constrain(prefs.getInt("px_mus_eff", 0), 0, (int)MUSIC_FX_COUNT - 1);
        _ssyncLightOn = prefs.getBool("px_ssy_on", true);
        _ssyncEffect = (SSyncEffectPreset)constrain(prefs.getInt("px_ssy_eff", 0), 0, (int)SSYNC_FX_COUNT - 1);
        _freqResp = (FreqResponse)constrain(prefs.getInt("px_freq", 0), 0, (int)FREQ_RESP_COUNT - 1);

        if (_initialized) {
            FastLED.setBrightness(_brightness);
        }
    }

    void saveToPreferences(Preferences& prefs) {
        prefs.putInt("px_mode", (int)_mode);
        prefs.putInt("px_r", _r);
        prefs.putInt("px_g", _g);
        prefs.putInt("px_b", _b);
        prefs.putInt("px_target", _targetPixel);
        prefs.putInt("px_mask", _targetMask);
        prefs.putInt("px_bright", _brightness);
        prefs.putInt("px_speed", _speedMs);
        prefs.putBool("px_mus_on", _musicLightOn);
        prefs.putInt("px_mus_eff", (int)_musicEffect);
        prefs.putBool("px_ssy_on", _ssyncLightOn);
        prefs.putInt("px_ssy_eff", (int)_ssyncEffect);
        prefs.putInt("px_freq", (int)_freqResp);
    }

    // ── Getters & Setters ─────────────────────────────────────────
    PixelMode getMode() const { return _mode; }
    void setMode(PixelMode m) { _mode = (PixelMode)constrain((int)m, 0, (int)PIXEL_MODE_COUNT - 1); }

    uint8_t getR() const { return _r; }
    uint8_t getG() const { return _g; }
    uint8_t getB() const { return _b; }
    CRGB getColor() const { return CRGB(_r, _g, _b); }
    void setColor(uint8_t r, uint8_t g, uint8_t b) { _r = r; _g = g; _b = b; }

    uint8_t getTargetPixel() const { return _targetPixel; }
    void setTargetPixel(uint8_t t) {
        if (t == 8) {
            _targetPixel = 8;
            _targetMask = 0xFF;
        } else if (t < POKO_LED_COUNT) {
            _targetPixel = t;
            _targetMask = (1 << t);
        } else {
            _targetPixel = 255;
        }
    }

    uint8_t getTargetMask() const { return _targetMask; }
    void setTargetMask(uint8_t mask) {
        _targetMask = mask;
        if (_targetMask == 0xFF) {
            _targetPixel = 8;
        } else if ((_targetMask & (_targetMask - 1)) == 0 && _targetMask != 0) {
            for (uint8_t i = 0; i < POKO_LED_COUNT; i++) {
                if (_targetMask == (1 << i)) {
                    _targetPixel = i;
                    break;
                }
            }
        } else {
            _targetPixel = 255; // Arbitrary multi-LED selection from WEB
        }
    }

    void toggleTargetLed(uint8_t idx) {
        if (idx < POKO_LED_COUNT) {
            setTargetMask(_targetMask ^ (1 << idx));
        }
    }

    const char* getTargetMaskLabel() const {
        if (_targetMask == 0xFF) return "All 8";
        if (_targetMask == 0) return "None";
        if ((_targetMask & (_targetMask - 1)) == 0) {
            for (uint8_t k = 0; k < 8; k++) {
                if (_targetMask == (1 << k)) {
                    static char buf[10];
                    snprintf(buf, sizeof(buf), "LED %d", k + 1);
                    return buf;
                }
            }
        }
        return "WEB";
    }

    uint8_t getBrightness() const { return _brightness; }
    void setBrightness(uint8_t b) {
        _brightness = constrain(b, 1, 255);
        if (_initialized) FastLED.setBrightness(_brightness);
    }

    uint16_t getSpeed() const { return _speedMs; }
    void setSpeed(uint16_t s) { _speedMs = constrain(s, 20, 2000); }

    bool getMusicLightOn() const { return _musicLightOn; }
    void setMusicLightOn(bool on) { _musicLightOn = on; }

    MusicEffectPreset getMusicEffect() const { return _musicEffect; }
    void setMusicEffect(MusicEffectPreset fx) { _musicEffect = fx; }

    void setSongProgress(float p) { _songProgress = constrain(p, 0.0f, 1.0f); }
    float getSongProgress() const { return _songProgress; }

    bool getSSyncLightOn() const { return _ssyncLightOn; }
    void setSSyncLightOn(bool on) { _ssyncLightOn = on; }

    SSyncEffectPreset getSSyncEffect() const { return _ssyncEffect; }
    void setSSyncEffect(SSyncEffectPreset fx) { _ssyncEffect = fx; }

    FreqResponse getFreqResponse() const { return _freqResp; }
    void setFreqResponse(FreqResponse f) { _freqResp = f; }

    CRGB getArtColor1() const { return _artColor1; }
    CRGB getArtColor2() const { return _artColor2; }
    void setArtColors(CRGB c1, CRGB c2) { _artColor1 = c1; _artColor2 = c2; }

    void setEffectiveVolume(int vol0to100) { _effectiveVolume = constrain(vol0to100, 0, 100); }
    int getEffectiveVolume() const { return _effectiveVolume; }

    // ── Frequency Filter & Audio Reactivity ──────────────────────
    inline void feedAudioSample(int16_t left, int16_t right) {
        int32_t mono = ((int32_t)left + (int32_t)right) / 2;
        float in = (float)mono / 32768.0f;

        // 1. Digital 2nd-order Low-Pass Filter (~220 Hz for Bass / Kick drum)
        _lp1 += 0.032f * (in - _lp1);
        _lp2 += 0.032f * (_lp1 - _lp2);

        // 2. High-Pass Filter (> 3000 Hz for Treble / Hi-hats)
        _hp = 0.70f * (_hp + in - _prevIn);
        _prevIn = in;

        // 3. Mid-Pass Filter (250 Hz - 3 kHz for Vocals & Melody)
        float mid = in - _lp2 - _hp;

        float filteredMag = 0.0f;
        switch (_freqResp) {
            case FREQ_RESP_LOW:
                filteredMag = fabsf(_lp2) * 3.4f;
                break;
            case FREQ_RESP_MID:
                filteredMag = fabsf(mid) * 2.2f;
                break;
            case FREQ_RESP_HIGH:
                filteredMag = fabsf(_hp) * 2.6f;
                break;
            case FREQ_RESP_ALL:
            default:
                filteredMag = fabsf(in);
                break;
        }

        if (filteredMag > _audioLevel) {
            _audioLevel = constrain(filteredMag, 0.0f, 1.0f);
        }
    }

    float getAudioLevel() const { return _audioLevel; }

    // ── Web OTA & ArduinoOTA Visualizer ──────────────────────────
    void showOtaProgress(float percent) {
        if (_otaErrorActive) return;
        uint32_t now = millis();
        percent = constrain(percent, 0.0f, 100.0f);

        // Throttle during transfer to ~25 FPS to preserve write throughput
        if (percent > 0.0f && percent < 100.0f && (now - _lastOtaShowAt < 40)) {
            return;
        }
        _lastOtaShowAt = now;
        _otaActive = true;

        float fillCount = (percent / 100.0f) * (float)POKO_LED_COUNT;

        for (uint8_t i = 0; i < POKO_LED_COUNT; i++) {
            float f = fillCount - (float)i;
            if (f <= 0.0f) {
                // Unlit/dim track
                _leds[i] = CRGB(6, 10, 16);
            } else if (f >= 1.0f) {
                // Completed segment: radiant emerald green
                _leds[i] = CRGB(0, 255, 60);
            } else {
                // Active transition LED: smooth blend
                uint8_t g = (uint8_t)(f * 255.0f);
                uint8_t r = (uint8_t)((1.0f - f) * 6.0f);
                uint8_t b = (uint8_t)((1.0f - f) * 16.0f);
                _leds[i] = CRGB(r, g, b);
            }
        }
        FastLED.show();
    }

    void showOtaError() {
        _otaActive = false;
        _otaErrorActive = true;
        _otaErrorStartedAt = millis();
        fill_solid(_leds, POKO_LED_COUNT, CRGB(255, 0, 0));
        FastLED.show();
    }

    void endOta() {
        _otaActive = false;
        _otaErrorActive = false;
    }

    bool isOtaActive() const { return _otaActive || _otaErrorActive; }

    // ── Album Art Color Extraction ───────────────────────────────
    void startColorExtraction() {
        memset(_colorBins, 0, sizeof(_colorBins));
        memset(_binSumR, 0, sizeof(_binSumR));
        memset(_binSumG, 0, sizeof(_binSumG));
        memset(_binSumB, 0, sizeof(_binSumB));
        _extracting = true;
    }

    void samplePixels(const uint16_t* rgb565, size_t count) {
        if (!_extracting || !rgb565) return;
        for (size_t i = 0; i < count; i++) {
            uint16_t p = rgb565[i];
            uint8_t r = ((p >> 11) & 0x1F) << 3;
            uint8_t g = ((p >> 5) & 0x3F) << 2;
            uint8_t b = (p & 0x1F) << 3;

            // Skip extreme near-black or extreme near-white
            if ((r < 20 && g < 20 && b < 20) || (r > 245 && g > 245 && b > 245)) {
                continue;
            }

            uint8_t bin = ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
            _colorBins[bin]++;
            _binSumR[bin] += r;
            _binSumG[bin] += g;
            _binSumB[bin] += b;
        }
    }

    void finishColorExtraction() {
        _extracting = false;

        // 1. Find dominant color 1
        uint16_t maxCount1 = 0;
        int bestBin1 = -1;
        for (int i = 0; i < 64; i++) {
            if (_colorBins[i] > maxCount1) {
                maxCount1 = _colorBins[i];
                bestBin1 = i;
            }
        }

        if (bestBin1 >= 0 && maxCount1 > 10) {
            _artColor1 = CRGB(
                _binSumR[bestBin1] / maxCount1,
                _binSumG[bestBin1] / maxCount1,
                _binSumB[bestBin1] / maxCount1
            );
        } else {
            _artColor1 = CRGB(0x16, 0x8B, 0xFF); // Azure Blue default
        }

        // 2. Find second dominant color that has distinct perceptual contrast
        uint16_t maxCount2 = 0;
        int bestBin2 = -1;
        for (int i = 0; i < 64; i++) {
            if (i == bestBin1 || _colorBins[i] == 0) continue;
            CRGB cand(
                _binSumR[i] / _colorBins[i],
                _binSumG[i] / _colorBins[i],
                _binSumB[i] / _colorBins[i]
            );
            if (colorDistance(_artColor1, cand) >= 70.0f) {
                if (_colorBins[i] > maxCount2) {
                    maxCount2 = _colorBins[i];
                    bestBin2 = i;
                }
            }
        }

        if (bestBin2 >= 0 && maxCount2 > 8) {
            _artColor2 = CRGB(
                _binSumR[bestBin2] / maxCount2,
                _binSumG[bestBin2] / maxCount2,
                _binSumB[bestBin2] / maxCount2
            );
        } else {
            // Fallback: Generate complementary or vibrant triad contrast
            CHSV hsv = rgb2hsv_approximate(_artColor1);
            hsv.hue += 96; // ~135 degree hue rotation
            hsv.sat = max((uint8_t)200, hsv.sat);
            hsv.val = max((uint8_t)220, hsv.val);
            _artColor2 = hsv;
        }

        Serial.printf("[pixel] Extracted Art Colors: #%02X%02X%02X and #%02X%02X%02X (dist=%.1f)\n",
            _artColor1.r, _artColor1.g, _artColor1.b,
            _artColor2.r, _artColor2.g, _artColor2.b,
            colorDistance(_artColor1, _artColor2));
    }

    // ── Pixel Colors Readout ──────────────────────────────────────
    CRGB getPixelColor(uint8_t i) const {
        if (i >= POKO_LED_COUNT) return CRGB::Black;
        return _leds[i];
    }

    // ── Animation Engine Update ───────────────────────────────────
    void update(bool isMusicPlaying, bool isSSyncPlaying) {
        if (!_initialized) return;

        uint32_t now = millis();

        // Check OTA states first
        if (_otaErrorActive) {
            uint32_t elapsed = now - _otaErrorStartedAt;
            if (elapsed >= 2500) {
                _otaErrorActive = false;
                _otaActive = false;
            } else {
                bool blinkOn = ((elapsed / 120) % 2) == 0;
                fill_solid(_leds, POKO_LED_COUNT, blinkOn ? CRGB(255, 0, 0) : CRGB::Black);
                FastLED.show();
                return;
            }
        }
        if (_otaActive) return;

        if (now - _lastFrameMs < 18) return; // ~55 FPS
        _lastFrameMs = now;

        // Smooth audio decay
        _audioLevel *= 0.88f;
        if (_audioLevel < 0.02f) _audioLevel = 0.0f;

        // Effective volume scaling (0.0 .. 1.0)
        float volScale = (float)_effectiveVolume / 100.0f;

        // Priority 1: Music Playback Reactivity (when Music is active & enabled)
        if (isMusicPlaying && _musicLightOn) {
            renderMusicSync(now, volScale);
            FastLED.show();
            return;
        }

        // Priority 2: SSync / Snapcast Reactivity (when SSync is active & enabled)
        if (isSSyncPlaying && _ssyncLightOn) {
            renderSSyncSync(now, volScale);
            FastLED.show();
            return;
        }

        // Priority 3: Manual Ambient Mode
        switch (_mode) {
            case PIXEL_MODE_OFF:
                fill_solid(_leds, POKO_LED_COUNT, CRGB::Black);
                break;

            case PIXEL_MODE_SOLID:
                renderSolid();
                break;

            case PIXEL_MODE_SPINNER:
                renderSpinner(now);
                break;

            case PIXEL_MODE_RAINBOW:
                renderRainbow(now);
                break;

            case PIXEL_MODE_BREATHE:
                renderBreathe(now);
                break;

            case PIXEL_MODE_FIRE:
                renderFire(now);
                break;

            default:
                renderSpinner(now);
                break;
        }

        // Apply target LED mask to ambient lighting if not all 8 LEDs
        if (_targetMask != 0xFF && _mode != PIXEL_MODE_OFF) {
            for (uint8_t i = 0; i < POKO_LED_COUNT; i++) {
                if (!(_targetMask & (1 << i))) {
                    _leds[i] = CRGB::Black;
                }
            }
        }

        FastLED.show();
    }

private:
    void renderSolid() {
        CRGB c(_r, _g, _b);
        for (uint8_t i = 0; i < POKO_LED_COUNT; i++) {
            if (_targetMask & (1 << i)) {
                _leds[i] = c;
            } else {
                _leds[i] = CRGB::Black;
            }
        }
    }

    void renderSpinner(uint32_t now) {
        _spinnerPos += 0.18f;
        if (_spinnerPos >= (float)POKO_LED_COUNT) _spinnerPos -= (float)POKO_LED_COUNT;

        CRGB c(_r, _g, _b);
        if (c.r == 0 && c.g == 0 && c.b == 0) c = CRGB(0, 200, 255);

        for (int i = 0; i < POKO_LED_COUNT; i++) {
            float dist = fabsf((float)i - _spinnerPos);
            if (dist > (float)POKO_LED_COUNT / 2.0f) dist = (float)POKO_LED_COUNT - dist;
            float intensity = 1.0f - (dist / 3.0f);
            if (intensity < 0.0f) intensity = 0.0f;
            intensity = intensity * intensity; // Smooth quadratic falloff

            _leds[i] = CRGB(
                (uint8_t)(c.r * intensity),
                (uint8_t)(c.g * intensity),
                (uint8_t)(c.b * intensity)
            );
        }
    }

    void renderRainbow(uint32_t now) {
        uint8_t baseHue = (now / 15) % 256;
        for (int i = 0; i < POKO_LED_COUNT; i++) {
            uint8_t hue = baseHue + (i * 256 / POKO_LED_COUNT);
            _leds[i] = CHSV(hue, 240, 255);
        }
    }

    void renderBreathe(uint32_t now) {
        float phase = (float)(now % 2400) / 2400.0f;
        float b = 0.5f - 0.5f * cosf(phase * 6.2831853f);
        b = b * b; // Perceptually even curve

        CRGB c(_r, _g, _b);
        if (c.r == 0 && c.g == 0 && c.b == 0) c = CRGB(0, 200, 255);

        for (int i = 0; i < POKO_LED_COUNT; i++) {
            _leds[i] = CRGB((uint8_t)(c.r * b), (uint8_t)(c.g * b), (uint8_t)(c.b * b));
        }
    }

    void renderFire(uint32_t now) {
        for (int i = 0; i < POKO_LED_COUNT; i++) {
            uint8_t flicker = random(120, 255);
            uint8_t r = flicker;
            uint8_t g = flicker / 3;
            uint8_t b = flicker / 16;
            _leds[i] = CRGB(r, g, b);
        }
    }

    void renderMusicSync(uint32_t now, float volScale) {
        // Resolve primary & secondary colors
        CRGB c1, c2;
        switch (_musicEffect) {
            case MUSIC_FX_AUTO:
            case MUSIC_FX_PROGRESS:
                c1 = _artColor1;
                c2 = _artColor2;
                break;
            case MUSIC_FX_RED:
                c1 = CRGB(255, 20, 40); c2 = CRGB(255, 120, 60);
                break;
            case MUSIC_FX_GREEN:
                c1 = CRGB(0, 255, 120); c2 = CRGB(60, 220, 255);
                break;
            case MUSIC_FX_BLUE:
                c1 = CRGB(20, 80, 255); c2 = CRGB(160, 40, 255);
                break;
            case MUSIC_FX_CYAN:
                c1 = CRGB(0, 230, 255); c2 = CRGB(0, 100, 255);
                break;
            case MUSIC_FX_PURPLE:
                c1 = CRGB(180, 40, 255); c2 = CRGB(255, 60, 140);
                break;
            case MUSIC_FX_AMBER:
                c1 = CRGB(255, 140, 0); c2 = CRGB(255, 60, 0);
                break;
            case MUSIC_FX_RAINBOW: {
                uint8_t h1 = (now / 20) % 256;
                c1 = CHSV(h1, 240, 255);
                c2 = CHSV(h1 + 128, 240, 255);
                break;
            }
            default:
                c1 = _artColor1; c2 = _artColor2;
                break;
        }

        float baselineGlow = 0.08f * volScale;
        float beatGlow = _audioLevel * volScale;
        float totalIntensity = constrain(baselineGlow + beatGlow * 0.92f, 0.0f, 1.0f);

        // Special Playback Progress Effect (like 5Pixels):
        // Fills ring as song progresses from c2 (background) to c1 (progress), pulsing with beat!
        if (_musicEffect == MUSIC_FX_PROGRESS) {
            float totalLeds = _songProgress * (float)POKO_LED_COUNT;
            for (int i = 0; i < POKO_LED_COUNT; i++) {
                float fill = totalLeds - (float)i;
                CRGB target;
                if (fill <= 0.0f) {
                    // Unplayed segment: secondary background color
                    target = c2;
                } else if (fill >= 1.0f) {
                    // Played segment: primary color
                    target = c1;
                } else {
                    // Blended transition LED
                    target = CRGB(
                        (uint8_t)(c2.r + (c1.r - c2.r) * fill),
                        (uint8_t)(c2.g + (c1.g - c2.g) * fill),
                        (uint8_t)(c2.b + (c1.b - c2.b) * fill)
                    );
                }
                // Pulsate with audio beat and volume
                _leds[i] = CRGB(
                    (uint8_t)(target.r * totalIntensity),
                    (uint8_t)(target.g * totalIntensity),
                    (uint8_t)(target.b * totalIntensity)
                );
            }
            return;
        }

        // Circular dual-color wave rotating with music beat
        _spinnerPos += 0.08f + (_audioLevel * 0.25f);
        if (_spinnerPos >= (float)POKO_LED_COUNT) _spinnerPos -= (float)POKO_LED_COUNT;

        for (int i = 0; i < POKO_LED_COUNT; i++) {
            float phase = fmodf((float)i + _spinnerPos, (float)POKO_LED_COUNT) / (float)POKO_LED_COUNT;
            float blend = 0.5f + 0.5f * sinf(phase * 6.2831853f);

            // Interpolate c1 <-> c2
            uint8_t r = (uint8_t)(c1.r + (c2.r - c1.r) * blend);
            uint8_t g = (uint8_t)(c1.g + (c2.g - c1.g) * blend);
            uint8_t b = (uint8_t)(c1.b + (c2.b - c1.b) * blend);

            // Scale by total intensity (low when quiet, radiant on beat)
            _leds[i] = CRGB(
                (uint8_t)(r * totalIntensity),
                (uint8_t)(g * totalIntensity),
                (uint8_t)(b * totalIntensity)
            );
        }
    }

    void renderSSyncSync(uint32_t now, float volScale) {
        float baselineGlow = 0.08f * volScale;
        float beatGlow = _audioLevel * volScale;
        float totalIntensity = constrain(baselineGlow + beatGlow * 0.92f, 0.0f, 1.0f);

        // Circular VU meter effect: number of LEDs lit corresponds to level
        float activeCount = totalIntensity * (float)POKO_LED_COUNT;

        for (int i = 0; i < POKO_LED_COUNT; i++) {
            CRGB col;
            switch (_ssyncEffect) {
                case SSYNC_FX_VOL_HUE: {
                    // Adaptive Hue: low volume = Cyan/Green (120), mid = Amber (45), high = Red (0)
                    float h = 135.0f - (volScale * 135.0f); // 135 (green) -> 0 (red)
                    col = CHSV((uint8_t)h, 240, 255);
                    break;
                }
                case SSYNC_FX_RAINBOW: {
                    uint8_t hue = ((now / 20) + (i * 32)) % 256;
                    col = CHSV(hue, 240, 255);
                    break;
                }
                case SSYNC_FX_CYAN:
                    col = CRGB(0, 230, 255);
                    break;
                case SSYNC_FX_MAGENTA:
                    col = CRGB(255, 40, 180);
                    break;
                case SSYNC_FX_AMBER:
                    col = CRGB(255, 140, 0);
                    break;
                default:
                    col = CRGB(0, 200, 255);
                    break;
            }

            float fill = activeCount - (float)i;
            if (fill <= 0.0f) {
                _leds[i] = CRGB((uint8_t)(col.r * 0.05f * volScale),
                                (uint8_t)(col.g * 0.05f * volScale),
                                (uint8_t)(col.b * 0.05f * volScale));
            } else if (fill >= 1.0f) {
                _leds[i] = col;
            } else {
                _leds[i] = CRGB((uint8_t)(col.r * fill), (uint8_t)(col.g * fill), (uint8_t)(col.b * fill));
            }
        }
    }
};

inline PixelEngine pixelEngine;
