#pragma once
#include "AcousticDSP.h"

// Main-loop presentation state: bounded smoothing/hold, never inventing notes.
class AcousticTunerReading {
public:
    AcousticTunerReading();
    void reset();
    void update(const AcousticDSP::Result& signal,uint32_t now,bool fresh);
    const AcousticDSP::Result& result() const;
private:
    AcousticDSP::Result _display;
    float _history[3];
    unsigned _historyCount,_historyWrite,_candidateCount;
    float _candidateConfidence;
    int _candidateRoot;
    AcousticDSP::Chord _candidateChord;
    uint32_t _lastNote,_lastChord,_lastCandidate;
};

class AcousticSpectrumRange {
public:
    AcousticSpectrumRange();
    float update(float peakDb,uint32_t now);
private:
    float _top;
    uint32_t _quietSince;
    bool _initialized,_quiet;
};

// Only tonal signals above the learned ambient floor can raise gain. Automatic
// adjustments are transient; the app persists only explicit manual changes.
class AcousticAutoGain {
public:
    AcousticAutoGain();
    void reset(uint32_t now);
    void manualOverride(uint32_t now);
    bool automatic(uint32_t now) const;
    int update(const AcousticDSP::Result& signal,int gain,uint32_t now);
private:
    float _noiseDb;
    uint32_t _lastChange,_manualAt;
    bool _manual;
};

uint16_t acousticBlend(uint16_t from,uint16_t to,float amount);
uint16_t acousticTuningColor(float cents,bool dark);
uint16_t acousticLevelColor(float db,bool dark);
uint16_t acousticBandColor(unsigned band,unsigned count,bool dark);
