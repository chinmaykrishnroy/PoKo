#pragma once
#include <stddef.h>
#include <stdint.h>

// Main-loop-owned processing. No hardware calls, allocations, or worker sharing.
class AcousticDSP {
public:
    static constexpr size_t MAX_SAMPLES = 8192;
    static constexpr size_t SPECTRUM_BARS = 96;
    enum Chord { NONE = -1, MAJOR, MINOR, SUS2, SUS4, SEVENTH, MAJOR7, MINOR7, AUGMENTED };
    struct Result {
        float rmsDb, peakDb, peak, frequency, cents, confidence, chordConfidence, spectralPeakHz, periodMs;
        int midi, root;
        Chord chord;
        bool clipped;
    };
    AcousticDSP();
    void push(const int16_t* samples, size_t count);
    bool analyze(size_t window, bool pitch, bool chord);
    const Result& result() const;
    const float* spectrum() const;
    float octaveDb(unsigned band, unsigned divisions) const;
    void waveform(size_t span, int16_t* low, int16_t* high, size_t columns) const;
    static const char* noteName(int midi);
    static const char* chordSuffix(Chord chord);
    static float niceRange(float peak);
private:
    int16_t _samples[MAX_SAMPLES];
    float _real[MAX_SAMPLES], _imag[MAX_SAMPLES], _pitchSamples[MAX_SAMPLES / 4], _difference[302];
    float _spectrum[SPECTRUM_BARS];
    size_t _write, _count, _window;
    float _windowEnergy;
    Result _result;
    int16_t sample(size_t offset) const;
    void fft();
    void detectPitch();
    void detectChord();
};

class AcousticScale {
public:
    AcousticScale();
    float update(float peak, uint32_t now);
private:
    float _range;
    uint32_t _quietSince;
    bool _quiet;
};
