#pragma once
#include <stddef.h>
#include <stdint.h>

// Main-loop-owned, allocation-free polyphonic fundamental estimator.
class NoteEstimator {
public:
    static constexpr unsigned FIRST_MIDI=36, LAST_MIDI=107, CANDIDATES=LAST_MIDI-FIRST_MIDI+1;
    static constexpr unsigned MAX_PEAKS=96, MAX_NOTES=8, HARMONICS=16;
    enum Profile { GUITAR, KEYBOARD };
    struct Peak { float frequency,amplitude,binHz; };
    struct Note {
        int midi=-1;
        float frequency=0,strength=0,confidence=0; // Salience and evidence score; not probabilities.
        bool observed=false,current=false;
    };
    struct Frame {
        Note notes[MAX_NOTES];
        unsigned count=0;
        int bassMidi=-1;
        float tuningCents=0,chroma[12]={},currentChroma[12]={};
        bool onset=false,bassUsed=false;
        uint32_t onsetAgeMs=UINT32_MAX;
    };
    NoteEstimator();
    void reset();
    bool configure(float referenceHz,Profile profile);
    const Frame& process(const Peak* fast,unsigned fastCount,const Peak* bass,unsigned bassCount,
                         uint32_t timeMs,float bassWindowMs,float rmsDb);
private:
    struct Hypothesis {
        uint8_t peak[HARMONICS];
        uint8_t quality[HARMONICS];
        float weight[HARMONICS],frequency,norm,coefficient,stretch;
        unsigned matches,strongMatches;
        bool observed;
    };
    Hypothesis _hypotheses[CANDIDATES];
    Peak _peaks[MAX_PEAKS];
    float _target[MAX_PEAKS],_residual[MAX_PEAKS],_tracked[CANDIDATES],_previousFast[12];
    float _harmonics[HARMONICS];
    float _centers[CANDIDATES],_stretchFactors[5][HARMONICS];
    float _previousNotes[12];
    float _reference=440,_previousDb=-120;
    Profile _profile=GUITAR;
    uint32_t _lastTime=0,_onsetTime=0;
    bool _history=false,_haveOnset=false;
    int _singleFundamental=-1;
    Frame _frame;
    int nearest(float frequency,unsigned count) const;
    void build(unsigned count,float offset);
    void solve(unsigned count);
    void estimate(unsigned count,float offset,float* raw);
};
