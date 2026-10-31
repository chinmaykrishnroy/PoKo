#pragma once
#include <stddef.h>
#include <stdint.h>
#include "NoteEstimator.h"

// Main-loop-owned processing. No hardware calls, allocations, or worker sharing.
class AcousticDSP {
public:
    static constexpr size_t MAX_SAMPLES = 8192;
    static constexpr size_t SPECTRUM_BARS = 96;
    enum Chord { NONE = -1, MAJOR, MINOR, SUS2, SUS4, SEVENTH, MAJOR7, MINOR7, AUGMENTED,
                 DIMINISHED, DIMINISHED7, HALF_DIMINISHED, MAJOR6, MINOR6, ADD9, MINOR_ADD9, POWER_CHORD };
    static constexpr unsigned CHORD_COUNT=16;
    enum ChordDecision { INSUFFICIENT, NO_CHORD, AMBIGUOUS, MATCH };
    struct ChordCandidate {
        int root = -1;
        Chord chord = NONE;
        float score = 0;
    };
    struct Result {
        float rmsDb, peakDb, peak, frequency, cents, confidence, chordConfidence, spectralPeakHz, periodMs;
        float tonality, binHz, maxHz, chordTuningCents;
        int midi, root;
        Chord chord;
        bool atFullScale; // Original integer rail reached; not proof of clipping.
        ChordCandidate bestChord, runnerUpChord;
        float chordMargin = 0;
        ChordDecision chordDecision = INSUFFICIENT;
        NoteEstimator::Note notes[NoteEstimator::MAX_NOTES];
        unsigned noteCount=0;
        int bassMidi=-1;
        bool onset=false,bassReady=false;
        uint32_t onsetAgeMs=UINT32_MAX;
        float bassBinHz=0,bassWindowMs=0,referenceHz=440;
    };
    explicit AcousticDSP(bool collectBass = true);
    void resetInputHistory();
    bool setSampleRate(uint32_t rate);
    bool configureTuning(float referenceHz,NoteEstimator::Profile profile);
    void enableChordAnalysis(bool enabled);
    void push(const int16_t* samples, size_t count);
    bool analyze(size_t window, bool pitch, bool chord, bool bass = false);
    const Result& result() const;
    const float* spectrum() const;
    float octaveDb(unsigned band, unsigned divisions) const;
    void waveform(size_t span, int16_t* low, int16_t* high, size_t columns) const;
    void waveformTrace(size_t span, float* values, size_t columns) const;
    static const char* noteName(int midi);
    static const char* chordSuffix(Chord chord);
    static const char* chordDecisionName(ChordDecision decision);
    static void formatChord(char* output,size_t capacity,int root,Chord chord,int bassMidi=-1);
private:
    int16_t _samples[MAX_SAMPLES];
    int16_t _bassSamples[MAX_SAMPLES];
    float _decimator[63];
    float _real[MAX_SAMPLES], _imag[MAX_SAMPLES], _pitchSamples[MAX_SAMPLES / 4], _difference[302];
    float _spectrum[SPECTRUM_BARS];
    size_t _write, _count, _window;
    size_t _bassWrite, _bassCount;
    unsigned _decimationPhase, _rawFullScaleAge;
    bool _bass, _collectBass;
    float _inputRate, _sampleRate, _frequencyMax, _tonalMaximum, _tonalNoise;
    float _windowEnergy, _spectralMaximum;
    bool _baseCollectBass,_chordEnabled=false;
    uint64_t _inputFrames=0,_lastBassFrames=0;
    uint64_t _bassValidAfterFrames=0;
    bool _octavesCached=false;
    unsigned _bassPeakCount=0;
    NoteEstimator::Peak _bassPeaks[32];
    float _octaveLevels[38],_referenceHz=440;
    NoteEstimator _notes;
    Result _result;
    int16_t sample(size_t offset) const;
    void fft();
    void detectPitch();
    void detectChord();
    unsigned extractPeaks(NoteEstimator::Peak* peaks,unsigned capacity,float maximum) const;
    float calculateOctaveDb(unsigned band,unsigned divisions) const;
};

class AcousticScale {
public:
    AcousticScale();
    float update(float peak, uint32_t now);
private:
    float _range;
    uint32_t _quietSince;
    bool _quiet;
    bool _initialized;
};
