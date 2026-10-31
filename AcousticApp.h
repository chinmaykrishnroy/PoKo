#pragma once
#include <Arduino_GFX_Library.h>
#include "CoreIApp.h"
#include "PokoAppState.h"
#include "AcousticDSP.h"
#include "AcousticPresentation.h"

// Foreground tools observe playback PCM without owning the output, or acquire
// the microphone when idle. DSP/canvas memory is released when the tool closes.
class AcousticApp : public IApp {
public:
    struct Status {
        bool capturing, hasResult, automaticGain;
        uint8_t mode;
        uint32_t frames, lastDataAgeMs;
        uint32_t capturedAtMs, analysisUs;
        uint32_t sampleRate, inputOverruns;
        const char* inputSource;
        const char* error;
        AcousticDSP::Result signal, displayed;
    };
    AcousticApp(Arduino_GFX* gfx, AppSwitchFn exitFn, bool tuner);
    ~AcousticApp() override;
    void begin() override;
    void load() override;
    void unload() override;
    void flushPreferences();
    void update() override;
    void refreshTheme() override;
    void renderToCanvas() override;
    void handleInput(InputEvent event) override;
    const char* appName() const override;
    bool wantsHoldRepeat() const override;
    bool isSafeForRestore() const override;
    bool isCapturing() const;
    Status status() const;
    void onLeft();
    void onRight();
    void onHoldingLeft();
    void onHoldingRight();
    void onEnter();
private:
    Arduino_GFX* _gfx;
    AppSwitchFn _exit;
    bool _tuner, _loaded = false, _dirty = true, _prefsDirty = false;
    Arduino_Canvas* _canvas = nullptr;
    AcousticDSP* _dsp = nullptr;
    AcousticScale _scale;
    AcousticSpectrumRange _spectrumRange;
    AcousticTunerReading _reading;
    AcousticAutoGain _autoGain;
    uint8_t _mode = 0, _time = 1, _fft = 3, _octave = 0, _response = 0;
    int _gain = 18, _preferredGain = 18;
    const char* _error = nullptr;
    uint32_t _lastData = 0, _lastAnalysis = 0, _lastDraw = 0, _lastEdit = 0;
    bool _hasResult = false, _playback = false;
    int _playbackSource = 0;
    uint32_t _inputRate = 48000, _inputGeneration = 0, _micSince = 0, _resumedAt = 0;
    uint32_t _frames = 0;
    uint32_t _rxOverruns = 0, _analysisUs = 0;
    float _range = 0.1f, _meter = -120, _periodMs = 0;
    bool selectInput();
    const char* inputName() const;
    void clearReading();
    void adjustParameter(int direction);
    void adjustGain(int direction);
    void text(int x, int y, const char* value, uint16_t color);
    void drawScope();
    void drawSpectrum(bool octave);
    void drawMeter();
    void drawTuner();
};
