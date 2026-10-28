#include "AcousticPresentation.h"
#include <algorithm>
#include <math.h>

AcousticTunerReading::AcousticTunerReading() { reset(); }
void AcousticTunerReading::reset() {
    _display={};_display.midi=_display.root=-1;_display.chord=AcousticDSP::NONE;
    _historyCount=_historyWrite=_candidateCount=0;_candidateRoot=-1;_candidateChord=AcousticDSP::NONE;
    _lastNote=_lastChord=_lastCandidate=0;
}
const AcousticDSP::Result& AcousticTunerReading::result() const { return _display; }
void AcousticTunerReading::update(const AcousticDSP::Result& signal,uint32_t now,bool fresh) {
    if(!fresh) { reset();return; }
    _display.rmsDb=signal.rmsDb;_display.clipped=signal.clipped;
    if(signal.frequency>0 && signal.confidence>=0.85f) {
        if(_display.frequency<=0 || fabsf(1200*log2f(signal.frequency/_display.frequency))>60) _historyCount=_historyWrite=0;
        _history[_historyWrite]=signal.frequency;_historyWrite=(_historyWrite+1)%3;
        _historyCount=std::min(3u,_historyCount+1);
        float sorted[3];for(unsigned i=0;i<_historyCount;++i) sorted[i]=_history[i];
        std::sort(sorted,sorted+_historyCount);float value=sorted[_historyCount/2];
        _display.frequency=_historyCount==1?value:_display.frequency+0.35f*(value-_display.frequency);
        float note=69+12*log2f(_display.frequency/440);
        _display.midi=(int)lroundf(note);_display.cents=100*(note-_display.midi);
        _display.confidence=signal.confidence;_lastNote=now;
    } else if(now-_lastNote>=250) { _display.frequency=_display.cents=_display.confidence=0;_display.midi=-1;_historyCount=0; }
    if(signal.chord!=AcousticDSP::NONE && signal.root>=0 && signal.chordConfidence>=0.45f) {
        if(signal.root==_display.root && signal.chord==_display.chord) {
            _lastChord=now;_display.chordConfidence+=0.35f*(signal.chordConfidence-_display.chordConfidence);
        }
        if(_candidateRoot!=signal.root || _candidateChord!=signal.chord || now-_lastCandidate>350) {
            _candidateRoot=signal.root;_candidateChord=signal.chord;_candidateCount=0;
        }
        _lastCandidate=now;_candidateCount=std::min(2u,_candidateCount+1);
        if(_candidateCount>=2) {
            _display.root=signal.root;_display.chord=signal.chord;
            _display.chordConfidence=signal.chordConfidence;_lastChord=now;
        }
    }
    if(now-_lastChord>=450) { _display.chord=AcousticDSP::NONE;_display.root=-1;_display.chordConfidence=0; }
}
AcousticSpectrumRange::AcousticSpectrumRange():_top(0),_quietSince(0),_initialized(false),_quiet(false) {}
float AcousticSpectrumRange::update(float peakDb,uint32_t now) {
    if(peakDb<=-119) return _top; // Digital silence must not fill the plot.
    float target=peakDb+12; // 12 dB headroom in a 60 dB plot = 80% height.
    // Spend the reserved headroom first; expand only when the peak overflows.
    if(!_initialized || peakDb>_top) { _top=target;_initialized=true;_quiet=false; }
    else if(target<_top-6) {
        if(!_quiet) { _quiet=true;_quietSince=now; }
        if(now-_quietSince>=750) { _top=target;_quiet=false; }
    } else _quiet=false;
    return _top;
}
AcousticAutoGain::AcousticAutoGain() { reset(0); }
void AcousticAutoGain::reset(uint32_t now) { _noiseDb=-80;_lastChange=now;_manualAt=0;_manual=false; }
void AcousticAutoGain::manualOverride(uint32_t now) { _manual=true;_manualAt=_lastChange=now; }
bool AcousticAutoGain::automatic(uint32_t now) const { return !_manual || now-_manualAt>=5000; }
int AcousticAutoGain::update(const AcousticDSP::Result& signal,int gain,uint32_t now) {
    if(signal.tonality<12) _noiseDb+=0.05f*(signal.rmsDb-_noiseDb);
    if(!automatic(now)) return gain;
    if((signal.clipped || signal.peakDb>-2) && now-_lastChange>=250 && gain>0) {
        _lastChange=now;_noiseDb-=3;return std::max(0,gain-3);
    }
    if(signal.rmsDb>-15 && now-_lastChange>=500 && gain>0) {
        _lastChange=now;_noiseDb-=3;return std::max(0,gain-3);
    }
    if(signal.tonality>=12 && signal.rmsDb>_noiseDb+8 && signal.rmsDb<-30 &&
       now-_lastChange>=1000 && gain<30) {
        _lastChange=now;_noiseDb+=3;return std::min(30,gain+3);
    }
    return gain;
}
uint16_t acousticBlend(uint16_t from,uint16_t to,float amount) {
    amount=std::max(0.0f,std::min(1.0f,amount));
    int r=(int)lroundf(((from>>11)&31)*(1-amount)+((to>>11)&31)*amount);
    int g=(int)lroundf(((from>>5)&63)*(1-amount)+((to>>5)&63)*amount);
    int b=(int)lroundf((from&31)*(1-amount)+(to&31)*amount);
    return (uint16_t)((r<<11)|(g<<5)|b);
}
uint16_t acousticTuningColor(float cents,bool dark) {
    float error=fabsf(cents);uint16_t green=dark?0x07E0:0x0400,amber=dark?0xFD20:0xB360,red=dark?0xF800:0xB000;
    if(error<=2) return green;
    if(error<=15) return acousticBlend(green,amber,(error-2)/13);
    return acousticBlend(amber,red,(error-15)/35);
}
uint16_t acousticLevelColor(float db,bool dark) {
    if(db>=-6) return dark?0xF800:0xB000;
    if(db>=-18) return dark?0xFD20:0xB360;
    return dark?0x07E0:0x0400;
}
uint16_t acousticBandColor(unsigned band,unsigned count,bool dark) {
    float position=count>1?(float)band/(count-1):0;
    return position<0.5f?acousticBlend(dark?0x07FF:0x0393,dark?0x549F:0x2255,position*2):
                         acousticBlend(dark?0x549F:0x2255,dark?0xE1FF:0x8014,(position-0.5f)*2);
}
