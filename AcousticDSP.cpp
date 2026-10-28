#include "AcousticDSP.h"
#include <math.h>
#include <algorithm>

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float RATE = 48000.0f;
float db(float amplitude) { return amplitude > 0.000001f ? 20.0f * log10f(amplitude) : -120.0f; }
float magnitude(float re, float im) { return sqrtf(re * re + im * im); }
}

AcousticDSP::AcousticDSP() : _write(0), _count(0), _window(0), _windowEnergy(1) {
    _result = {-120,-120,0,0,0,0,0,0,0,-1,-1,NONE,false};
    for (float& v : _spectrum) v = -120;
}
void AcousticDSP::push(const int16_t* data, size_t count) {
    for (size_t i=0;i<count;++i) { _samples[_write]=data[i];_write=(_write+1)%MAX_SAMPLES; }
    _count=std::min(MAX_SAMPLES,_count+count);
}
int16_t AcousticDSP::sample(size_t offset) const { return _samples[(_write+MAX_SAMPLES-_window+offset)%MAX_SAMPLES]; }
const AcousticDSP::Result& AcousticDSP::result() const { return _result; }
const float* AcousticDSP::spectrum() const { return _spectrum; }

bool AcousticDSP::analyze(size_t window, bool pitch, bool chord) {
    if(window<1024 || window>MAX_SAMPLES || (window&(window-1)) || _count<window) return false;
    _window=window;
    float mean=0;
    for(size_t i=0;i<window;++i) mean+=sample(i)/32768.0f;
    mean/=window;
    float energy=0, peak=0, windowSum=0;
    _windowEnergy=0;
    bool clipped=false;
    const float step=2*PI/(window-1), cs=cosf(step), sn=sinf(step);
    float c=1,s=0;
    for(size_t i=0;i<window;++i) {
        int16_t raw=sample(i);
        float value=raw/32768.0f-mean;
        energy+=value*value;peak=std::max(peak,fabsf(value));
        clipped|=raw>=32760 || raw<=-32760;
        float hann=0.5f-0.5f*c;
        _real[i]=value*hann;_imag[i]=0;
        windowSum+=hann;_windowEnergy+=hann*hann;
        float next=c*cs-s*sn;s=s*cs+c*sn;c=next;
    }
    _result={db(sqrtf(energy/window)),db(peak),peak,0,0,0,0,0,0,-1,-1,NONE,clipped};
    // Average full periods between interpolated rising zero crossings. A
    // Schmitt re-arm and interval-variance gate reject noisy/aperiodic signals.
    if(peak>0.0015f) {
        bool armed=false;float previous=sample(0)/32768.0f-mean,last=-1,sum=0,squares=0;
        unsigned periods=0;
        for(size_t i=1;i<window;++i) {
            float v=sample(i)/32768.0f-mean;
            if(v<-peak*0.15f) armed=true;
            if(armed && previous<0 && v>=0) {
                float crossing=(float)(i-1)-previous/(v-previous);
                if(last>=0) { float distance=crossing-last;sum+=distance;squares+=distance*distance;++periods; }
                last=crossing;armed=false;
            }
            previous=v;
        }
        if(periods>=3) {
            float average=sum/periods,variance=std::max(0.0f,squares/periods-average*average);
            if(average>=2.4f && variance<average*average*0.02f) _result.periodMs=1000*average/RATE;
        }
    }
    fft();
    // Dominant spectral peak (not necessarily a note's fundamental). Hann
    // log-magnitude interpolation gives sub-bin frequency for isolated tones.
    if(peak>0.0015f) {
        size_t first=std::max<size_t>(1,(size_t)ceilf(20*window/RATE));
        size_t last=std::min(window/2-1,(size_t)floorf(20000*window/RATE));
        size_t best=first;float maximum=0;
        for(size_t k=first;k<=last;++k) {
            float v=magnitude(_real[k],_imag[k]);
            if(v>maximum) { maximum=v;best=k; }
        }
        float a=logf(magnitude(_real[best-1],_imag[best-1])+1e-12f);
        float b=logf(maximum+1e-12f),c=logf(magnitude(_real[best+1],_imag[best+1])+1e-12f);
        float d=a-2*b+c,delta=fabsf(d)>1e-9f?0.5f*(a-c)/d:0;
        _result.spectralPeakHz=(best+std::max(-0.5f,std::min(0.5f,delta)))*RATE/window;
    }
    for(size_t bar=0;bar<SPECTRUM_BARS;++bar) {
        float low=20*powf(1000.0f,(float)bar/SPECTRUM_BARS);
        float high=20*powf(1000.0f,(float)(bar+1)/SPECTRUM_BARS);
        size_t a=std::max<size_t>(1,(size_t)floorf(low*window/RATE));
        size_t b=std::min(window/2,(size_t)ceilf(high*window/RATE));
        float power=0;
        for(size_t k=a;k<=b;++k) power=std::max(power,magnitude(_real[k],_imag[k]));
        _spectrum[bar]=db(2*power/windowSum);
    }
    if(peak>0.0015f) {
        if(pitch) detectPitch();
        if(chord) detectChord();
    }
    return true;
}

void AcousticDSP::fft() {
    for(size_t i=1,j=0;i<_window;++i) {
        size_t bit=_window>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j) { std::swap(_real[i],_real[j]);std::swap(_imag[i],_imag[j]); }
    }
    for(size_t length=2;length<=_window;length<<=1) {
        float angle=-2*PI/length, wr=cosf(angle),wi=sinf(angle);
        for(size_t start=0;start<_window;start+=length) {
            float r=1,im=0;
            for(size_t j=0;j<length/2;++j) {
                size_t a=start+j,b=a+length/2;
                float tr=_real[b]*r-_imag[b]*im,ti=_real[b]*im+_imag[b]*r;
                _real[b]=_real[a]-tr;_imag[b]=_imag[a]-ti;
                _real[a]+=tr;_imag[a]+=ti;
                float next=r*wr-im*wi;im=im*wr+r*wi;r=next;
            }
        }
    }
}

float AcousticDSP::octaveDb(unsigned band,unsigned divisions) const {
    if(!_window || (divisions!=1 && divisions!=3) || band>9*divisions) return -120;
    float center=31.25f*powf(2.0f,(float)band/divisions);
    float ratio=powf(2.0f,0.5f/divisions);
    size_t first=std::max<size_t>(1,(size_t)ceilf(center/ratio*_window/RATE));
    size_t last=std::min(_window/2,(size_t)floorf(center*ratio*_window/RATE));
    float sum=0;
    for(size_t k=first;k<=last;++k) sum+=_real[k]*_real[k]+_imag[k]*_imag[k];
    return db(sqrtf(2*sum/(_window*_windowEnergy)));
}

void AcousticDSP::detectPitch() {
    size_t n=_window/4;
    for(size_t i=0;i<n;++i) {
        float sum=0;for(size_t j=0;j<4;++j) sum+=sample(i*4+j)/32768.0f;
        _pitchSamples[i]=sum*0.25f;
    }
    const size_t length=std::min<size_t>(512,n/2);
    const size_t maximum=std::min<size_t>(300,n-length-1);
    float cumulative=0;
    _difference[0]=1;
    for(size_t lag=1;lag<=maximum;++lag) {
        float difference=0;
        for(size_t i=0;i<length;++i) { float d=_pitchSamples[i]-_pitchSamples[i+lag];difference+=d*d; }
        cumulative+=difference;
        _difference[lag]=cumulative>1e-12f ? difference*lag/cumulative : 1;
    }
    size_t selected=0;
    for(size_t lag=6;lag<maximum;++lag) if(_difference[lag]<0.15f) {
        while(lag+1<maximum && _difference[lag+1]<_difference[lag]) ++lag;
        selected=lag;break;
    }
    if(!selected) return;
    float a=_difference[selected-1],b=_difference[selected],c=_difference[selected+1];
    float denominator=a-2*b+c;
    float fraction=fabsf(denominator)>1e-9f ? 0.5f*(a-c)/denominator : 0;
    fraction=std::max(-1.0f,std::min(1.0f,fraction));
    float frequency=12000.0f/(selected+fraction);
    if(frequency<40 || frequency>2000) return;
    // Short periods make a quadratic YIN lag estimate biased at high notes.
    // Refine the already-selected fundamental with a log-Hann spectral peak.
    if(frequency>400) {
        size_t center=(size_t)lroundf(frequency*_window/RATE),best=center;
        float maximum=0;
        for(size_t k=center>3?center-3:1;k<=center+3 && k+1<_window/2;++k) {
            float v=magnitude(_real[k],_imag[k]);if(v>maximum) { maximum=v;best=k; }
        }
        if(best>0 && best+1<_window/2 && maximum>_result.peak*_window*0.01f) {
            float a=logf(magnitude(_real[best-1],_imag[best-1])+1e-12f);
            float b=logf(maximum+1e-12f),c=logf(magnitude(_real[best+1],_imag[best+1])+1e-12f);
            float d=a-2*b+c;
            float delta=fabsf(d)>1e-9f ? 0.5f*(a-c)/d : 0;
            frequency=(best+std::max(-0.5f,std::min(0.5f,delta)))*RATE/_window;
        }
    }
    float note=69+12*log2f(frequency/440.0f);
    _result.frequency=frequency;_result.midi=(int)lroundf(note);
    _result.cents=(note-_result.midi)*100;
    _result.confidence=std::max(0.0f,1-b);
}

void AcousticDSP::detectChord() {
    struct Peak { float frequency,amplitude; } peaks[64];
    size_t count=0;
    float strongest=0;
    size_t first=std::max<size_t>(2,(size_t)(55*_window/RATE));
    size_t last=std::min(_window/2-1,(size_t)(4000*_window/RATE));
    for(size_t k=first;k<last;++k) strongest=std::max(strongest,magnitude(_real[k],_imag[k]));
    for(size_t k=first;k<last && count<64;++k) {
        float a=magnitude(_real[k-1],_imag[k-1]),b=magnitude(_real[k],_imag[k]),c=magnitude(_real[k+1],_imag[k+1]);
        if(b<a || b<=c || b<strongest*0.06f) continue;
        a=logf(a+1e-12f);c=logf(c+1e-12f);float center=logf(b+1e-12f),d=a-2*center+c;
        float delta=fabsf(d)>1e-9f ? 0.5f*(a-c)/d : 0;
        peaks[count++]={(k+std::max(-0.5f,std::min(0.5f,delta)))*RATE/_window,b};
    }
    float chroma[12]={};
    int bass=-1;
    for(size_t i=0;i<count;++i) {
        float weight=peaks[i].amplitude;
        for(size_t j=0;j<i;++j) {
            float ratio=peaks[i].frequency/peaks[j].frequency,nearest=roundf(ratio);
            if(nearest>=2 && nearest<=6 && fabsf(ratio-nearest)<0.025f) { weight*=0.22f;break; }
        }
        float note=69+12*log2f(peaks[i].frequency/440.0f);
        int midi=(int)lroundf(note);
        if(fabsf(note-midi)>0.35f) continue;
        int pc=(midi%12+12)%12;
        if(bass<0 && peaks[i].amplitude>strongest*0.15f) bass=pc;
        chroma[pc]+=weight;
    }
    float maximum=0,total=0;for(float v:chroma) maximum=std::max(maximum,v);
    if(maximum<=0) return;
    int present=0;for(float& v:chroma) { v/=maximum;total+=v;present+=v>0.18f; }
    if(present<3 || present>7) return;
    const int patterns[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},
                             {0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
    float best=0;
    for(int root=0;root<12;++root) for(int quality=0;quality<8;++quality) {
        float inside=0,weakest=1;int notes=0;
        for(int interval:patterns[quality]) if(interval>=0) {
            float value=chroma[(root+interval)%12];inside+=value;weakest=std::min(weakest,value);++notes;
        }
        if(weakest<0.18f) continue;
        float coverage=inside/total;
        float score=inside/notes-0.6f*(total-inside)/(12-notes);
        if(root==bass) score+=0.015f;
        if(coverage>=0.7f && score>best) {
            best=score;_result.root=root;_result.chord=(Chord)quality;
            _result.chordConfidence=std::min(1.0f,coverage*inside/notes);
        }
    }
    if(best<0.5f) { _result.root=-1;_result.chord=NONE;_result.chordConfidence=0; }
}

void AcousticDSP::waveform(size_t span,int16_t* low,int16_t* high,size_t columns) const {
    if(!columns) return;
    span=std::min(span,_count);
    size_t lookback=std::min<size_t>(span/4,_count-span),start=(_write+MAX_SAMPLES-span-lookback)%MAX_SAMPLES;
    for(size_t i=1;i<lookback;++i) if(_samples[(start+i-1)%MAX_SAMPLES]<=0 && _samples[(start+i)%MAX_SAMPLES]>0) { start=(start+i)%MAX_SAMPLES;break; }
    for(size_t x=0;x<columns;++x) {
        size_t a=x*span/columns,b=std::min(span,std::max(a+1,(x+1)*span/columns));
        int16_t minimum=32767,maximum=-32768;
        for(size_t i=a;i<b;++i) { int16_t v=_samples[(start+i)%MAX_SAMPLES];minimum=std::min(minimum,v);maximum=std::max(maximum,v); }
        low[x]=span ? minimum : 0;high[x]=span ? maximum : 0;
    }
}
const char* AcousticDSP::noteName(int midi) {
    static const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return midi>=0 ? names[midi%12] : "--";
}
const char* AcousticDSP::chordSuffix(Chord chord) {
    static const char* names[]={"maj","m","sus2","sus4","7","maj7","m7","aug"};
    return chord>=MAJOR && chord<=AUGMENTED ? names[chord] : "";
}
float AcousticDSP::niceRange(float peak) {
    const float ranges[]={0.005f,0.01f,0.025f,0.05f,0.1f,0.25f,0.5f,1.0f,2.0f};
    for(float range:ranges) if(peak<=range) return range;
    return 2;
}
AcousticScale::AcousticScale() : _range(0.1f),_quietSince(0),_quiet(false) {}
float AcousticScale::update(float peak,uint32_t now) {
    float wanted=AcousticDSP::niceRange(peak*1.05f);
    if(wanted>_range) { _range=wanted;_quiet=false; }
    else if(wanted<_range && peak<_range*0.35f) {
        if(!_quiet) { _quiet=true;_quietSince=now; }
        if(uint32_t(now-_quietSince)>=1500) { _range=wanted;_quiet=false; }
    } else _quiet=false;
    return _range;
}
