#include "ReactiveLights.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI=3.14159265358979323846f;
constexpr float CROSSOVERS[3]={20,250,3000};
float clamp(float x) { return std::max(0.0f,std::min(1.0f,x)); }
float distance(float a,float b) { float d=fabsf(a-b);return std::min(d,8-d); }
}
void ReactiveLightInput::configure(uint32_t rate,bool spectrum) {
    _rate=rate;_spectrum=spectrum;
    for(auto& channel:_state) for(float& x:channel) x=0;
    for(auto& channel:_delay) for(auto& band:channel) for(float& x:band) x=0;
    for(unsigned k=0;k<3;++k) _alpha[k]=1-expf(-2*PI*std::min(CROSSOVERS[k],rate*.45f)/rate);
    if(spectrum) for(unsigned k=0;k<8;++k) {
        // Octave-spaced bandpasses, with low/high-pass filters at the ends.
        float frequency=k==0?88:k==7?5657:125.0f*(1u<<(k-1));
        float w=2*PI*std::min(frequency,rate*.45f)/rate,c=cosf(w);
        float a=sinf(w)*(k==0 || k==7?.70710678f:1.0f/2.4f);
        auto& b=_biquad[k];b[3]=-2*c/(1+a);b[4]=(1-a)/(1+a);
        if(k==0) { b[0]=(1-c)*.5f/(1+a);b[1]=2*b[0];b[2]=b[0]; }
        else if(k==7) { b[0]=(1+c)*.5f/(1+a);b[1]=-2*b[0];b[2]=b[0]; }
        else { b[0]=a/(1+a);b[1]=0;b[2]=-b[0]; }
    }
}
ReactiveLightInput::Frame ReactiveLightInput::process(const int16_t* stereo,size_t frames,uint32_t rate,
                                                      bool spectrum,uint32_t now,bool audible) {
    Frame result;result.spectrum=spectrum;
    if(!stereo || !frames || rate<8000 || rate>192000) return result;
    if(rate!=_rate || spectrum!=_spectrum || now-_lastMs>250) configure(rate,spectrum);
    _lastMs=now;
    for(size_t i=0;i<frames;++i) for(unsigned channel=0;channel<2;++channel) {
        float x=audible?stereo[i*2+channel]/32768.0f:0;
        auto& state=_state[channel];
        for(unsigned k=0;k<3;++k) state[k]+=_alpha[k]*(x-state[k]);
        float groups[]={state[1]-state[0],state[2]-state[1],x-state[2]};
        for(unsigned k=0;k<3;++k) result.groups[k]+=groups[k]*groups[k];
        if(spectrum) {
            float ac=x-state[0];
            for(unsigned k=0;k<8;++k) {
                const auto& b=_biquad[k];auto& z=_delay[channel][k];
                float band=b[0]*ac+z[0];z[0]=b[1]*ac-b[3]*band+z[1];z[1]=b[2]*ac-b[4]*band;
                result.bands[k]+=band*band;
            }
        }
    }
    float divisor=2.0f*frames;
    for(float& x:result.groups) x=sqrtf(x/divisor);
    for(float& x:result.bands) x=sqrtf(x/divisor);
    // Squaring each channel before combining preserves anti-phase stereo.
    return result;
}
void ReactiveLightPattern::render(const ReactiveLightInput::Frame& input,bool spectrum,uint8_t response,
                                  float volume,uint16_t speed,uint8_t mask,uint32_t now,RGB* output) {
    float dt=_initialized?std::min(250.0f,(float)(now-_lastMs)):18;
    _lastMs=now;_initialized=true;volume=clamp(volume);
    float peak=0;
    bool all=response>=3;
    if(spectrum) {
        for(unsigned i=0;i<8;++i) if(all || (response==0 && i<3) || (response==1 && i>=3 && i<6) || (response==2 && i>=6))
            peak=std::max(peak,input.bands[i]);
    } else for(unsigned i=0;i<3;++i) if(all || response==i) peak=std::max(peak,input.groups[i]);
    _reference=std::max(.025f,std::max(peak,_reference*expf(-dt/2500)));
    auto follow=[&](float target,float& level,float release) {
        target=target<=.0015f?0:clamp((target-.0015f)/_reference);
        level+=(1-expf(-dt/(target>level?25.0f:release)))*(target-level);
        if(level<.002f) level=0;
    };
    for(unsigned k=0;k<3;++k) follow(input.groups[k],_groups[k],k==2?100.0f:230.0f);
    for(unsigned k=0;k<8;++k) follow(input.bands[k],_bands[k],180);
    float low=all || response==0?_groups[0]:0;
    float mid=all || response==1?_groups[1]:0;
    float high=all || response==2?_groups[2]:0;
    _phase=fmodf(_phase+dt*(.0007f+.002f*low+.0015f*mid)*70/std::max(20u,(unsigned)speed),8.0f);
    _spark*=expf(-dt/100);
    if(high>.15f && high>_previousHigh+.06f) { _sparkLed=(_sparkLed+3)%8;_spark=high; }
    _previousHigh=high;
    constexpr uint8_t palette[8][3]={{65,85,255},{95,50,255},{180,45,245},{245,45,160},
                                  {30,210,190},{35,190,255},{255,185,35},{255,65,180}};
    for(unsigned i=0;i<8;++i) {
        float r=0,g=0,b=0;
        if(spectrum) {
            bool enabled=all || (response==0 && i<3) || (response==1 && i>=3 && i<6) || (response==2 && i>=6);
            float level=enabled?sqrtf(_bands[i])*volume:0;
            r=palette[i][0]*level;g=palette[i][1]*level;b=palette[i][2]*level;
        } else {
            float d=std::min(distance((float)i,_phase),distance((float)i,fmodf(8-_phase,8.0f)));
            float pulse=low*(.2f+.8f*expf(-d*d*1.2f));
            float ribbon=mid*(.35f+.65f*(.5f+.5f*sinf(i*PI/4-_phase*PI/4)));
            float glitter=(all || response==2)?high*.12f+(_sparkLed==i?_spark:0):0;
            float hue=.5f+.5f*sinf(i*PI/4-_phase*PI/4);
            r=((65+135*hue)*pulse+(30+200*hue)*ribbon+255*glitter)*volume;
            g=((80-45*hue)*pulse+(210-140*hue)*ribbon+(_sparkLed%2?65:200)*glitter)*volume;
            b=(255*pulse+(190+50*hue)*ribbon+(_sparkLed%2?195:65)*glitter)*volume;
        }
        float gain=std::min(1.0f,255/std::max(1.0f,std::max(r,std::max(g,b))));
        output[i]=(mask&(1u<<i))?RGB{(uint8_t)(r*gain),(uint8_t)(g*gain),(uint8_t)(b*gain)}:RGB{};
    }
}
