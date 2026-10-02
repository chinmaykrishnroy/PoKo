#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <atomic>
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
    std::atomic<FreqResponse> _freqResp{FREQ_RESP_LOW};
    portMUX_TYPE _audioFilterMux = portMUX_INITIALIZER_UNLOCKED;
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
    std::atomic<float> _audioLevel{0.0f}; // Published by audio tasks, consumed by loop task
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
    static float colorDistance(CRGB c1, CRGB c2);

public:
    PixelEngine();

    void begin();

    void loadFromPreferences(Preferences& prefs);

    void saveToPreferences(Preferences& prefs);

    // ── Getters & Setters ─────────────────────────────────────────
    PixelMode getMode() const;
    void setMode(PixelMode m);

    uint8_t getR() const;
    uint8_t getG() const;
    uint8_t getB() const;
    CRGB getColor() const;
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    uint8_t getTargetPixel() const;
    void setTargetPixel(uint8_t t);

    uint8_t getTargetMask() const;
    void setTargetMask(uint8_t mask);

    void toggleTargetLed(uint8_t idx);

    const char* getTargetMaskLabel() const;

    uint8_t getBrightness() const;
    void setBrightness(uint8_t b);

    uint16_t getSpeed() const;
    void setSpeed(uint16_t s);

    bool getMusicLightOn() const;
    void setMusicLightOn(bool on);

    MusicEffectPreset getMusicEffect() const;
    void setMusicEffect(MusicEffectPreset fx);

    void setSongProgress(float p);
    float getSongProgress() const;

    bool getSSyncLightOn() const;
    void setSSyncLightOn(bool on);

    SSyncEffectPreset getSSyncEffect() const;
    void setSSyncEffect(SSyncEffectPreset fx);

    FreqResponse getFreqResponse() const;
    void setFreqResponse(FreqResponse f);

    CRGB getArtColor1() const;
    CRGB getArtColor2() const;
    void setArtColors(CRGB c1, CRGB c2);

    void setEffectiveVolume(int vol0to100);
    int getEffectiveVolume() const;

    // ── Frequency Filter & Audio Reactivity ──────────────────────
    void feedAudioSample(int16_t left, int16_t right);

    float getAudioLevel() const;

    // ── Web OTA & ArduinoOTA Visualizer ──────────────────────────
    void showOtaProgress(float percent);

    void showOtaError();

    void endOta();

    bool isOtaActive() const;

    // ── Album Art Color Extraction ───────────────────────────────
    void startColorExtraction();

    void samplePixels(const uint16_t* rgb565, size_t count);

    void finishColorExtraction();

    // ── Pixel Colors Readout ──────────────────────────────────────
    CRGB getPixelColor(uint8_t i) const;

    // ── Animation Engine Update ───────────────────────────────────
    void update(bool isMusicPlaying, bool isSSyncPlaying);

private:
    void renderSolid();

    void renderSpinner(uint32_t now);

    void renderRainbow(uint32_t now);

    void renderBreathe(uint32_t now);

    void renderFire(uint32_t now);

    void renderMusicSync(uint32_t now, float volScale);

    void renderSSyncSync(uint32_t now, float volScale);
};

extern PixelEngine pixelEngine;

