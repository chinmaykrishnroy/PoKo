#pragma once
#include <stddef.h>
#include <stdint.h>

// Audio-task-owned filter state; only Frame snapshots cross to the main loop.
class ReactiveLightInput {
public:
    struct Frame { float groups[3]={}, bands[8]={}; bool spectrum=false; };
    Frame process(const int16_t* stereo,size_t frames,uint32_t rate,bool spectrum,uint32_t now,bool audible=true);
private:
    float _state[2][3]={},_alpha[3]={};
    float _biquad[8][5]={},_delay[2][8][2]={};
    uint32_t _rate=0,_lastMs=0;
    bool _spectrum=false;
    void configure(uint32_t rate,bool spectrum);
};

// Main-loop-owned animation. No hardware access, allocations or PCM mutation.
class ReactiveLightPattern {
public:
    struct RGB { uint8_t r=0,g=0,b=0; };
    void render(const ReactiveLightInput::Frame& input,bool spectrum,uint8_t response,
                float volume,uint16_t speed,uint8_t mask,uint32_t now,RGB* output);
private:
    float _groups[3]={},_bands[8]={},_reference=.03f,_phase=0,_spark=0,_previousHigh=0;
    uint32_t _lastMs=0;
    unsigned _sparkLed=0;
    bool _initialized=false;
};
