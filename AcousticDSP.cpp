#include "AcousticDSP.h"
#include <math.h>
#include <algorithm>

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float RATE = 48000.0f;
float db(float amplitude) { return amplitude > 0.000001f ? 20.0f * log10f(amplitude) : -120.0f; }
float magnitude(float re, float im) { return sqrtf(re * re + im * im); }
}

AcousticDSP::AcousticDSP(bool collectBass) : _write(0), _count(0), _window(0), _bassWrite(0), _bassCount(0),
    _decimationPhase(0), _rawClipAge(MAX_SAMPLES*4), _bass(false), _collectBass(collectBass), _sampleRate(RATE), _frequencyMax(20000),
    _tonalMaximum(0), _tonalNoise(0), _windowEnergy(1), _spectralMaximum(0) {
    _result={};_result.rmsDb=_result.peakDb=-120;_result.midi=_result.root=-1;_result.chord=NONE;
    for (float& v : _spectrum) v = -120;
    if(!_collectBass) return;
    // 63-tap Hamming low-pass before 4:1 decimation. Bass mode observes 683 ms
    // at 12 kHz with 8192 points (1.465 Hz bins), rather than zero padding.
    float sum=0;
    for(int i=0;i<63;++i) {
        int x=i-31;float sinc=x?sinf(2*PI*3000/RATE*x)/(PI*x):2*3000/RATE;
        _decimator[i]=sinc*(0.54f-0.46f*cosf(2*PI*i/62));sum+=_decimator[i];
    }
    for(float& coefficient:_decimator) coefficient/=sum;
}
void AcousticDSP::resetInputHistory() {
    _write=_count=_bassWrite=_bassCount=0;_decimationPhase=0;_rawClipAge=MAX_SAMPLES*4;
}
void AcousticDSP::push(const int16_t* data, size_t count) {
    for (size_t i=0;i<count;++i) {
        if(_rawClipAge<MAX_SAMPLES*4) ++_rawClipAge;
        if(data[i]>=32760 || data[i]<=-32760) _rawClipAge=0;
        _samples[_write]=data[i];_write=(_write+1)%MAX_SAMPLES;_count=std::min(MAX_SAMPLES,_count+1);
        if(_collectBass && ++_decimationPhase==4) {
            _decimationPhase=0;
            if(_count>=63) {
                float value=0;
                for(size_t j=0;j<63;++j) value+=_samples[(_write+MAX_SAMPLES-1-j)%MAX_SAMPLES]*_decimator[j];
                _bassSamples[_bassWrite]=(int16_t)std::max(-32768.0f,std::min(32767.0f,roundf(value)));
                _bassWrite=(_bassWrite+1)%MAX_SAMPLES;_bassCount=std::min(MAX_SAMPLES,_bassCount+1);
            }
        }
    }
}
int16_t AcousticDSP::sample(size_t offset) const {
    return _bass ? _bassSamples[(_bassWrite+MAX_SAMPLES-_window+offset)%MAX_SAMPLES] :
                   _samples[(_write+MAX_SAMPLES-_window+offset)%MAX_SAMPLES];
}
const AcousticDSP::Result& AcousticDSP::result() const { return _result; }
const float* AcousticDSP::spectrum() const { return _spectrum; }

bool AcousticDSP::analyze(size_t window, bool pitch, bool chord, bool bass) {
    if(window<1024 || window>MAX_SAMPLES || (window&(window-1)) || (bass?_bassCount:_count)<window) return false;
    _bass=bass;_sampleRate=bass?RATE/4:RATE;_frequencyMax=bass?2000.0f:20000.0f;
    _window=window;
    float mean=0;
    for(size_t i=0;i<window;++i) mean+=sample(i)/32768.0f;
    mean/=window;
    float energy=0, peak=0, windowSum=0;
    _windowEnergy=0;
    bool clipped=_rawClipAge<window*(bass?4:1);
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
    _result={};_result.rmsDb=db(sqrtf(energy/window));_result.peakDb=db(peak);_result.peak=peak;
    _result.midi=_result.root=-1;_result.chord=NONE;_result.clipped=clipped;
    _result.binHz=_sampleRate/window;_result.maxHz=_frequencyMax;
    // Average full periods between interpolated rising zero crossings. A
    // Schmitt re-arm and interval-variance gate reject noisy/aperiodic signals.
    if(_result.rmsDb>-90) {
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
            if(average>=2.4f && variance<average*average*0.02f) _result.periodMs=1000*average/_sampleRate;
        }
    }
    fft();
    _spectralMaximum=0;
    // Dominant spectral peak (not necessarily a note's fundamental). Hann
    // log-magnitude interpolation gives sub-bin frequency for isolated tones.
    if(_result.rmsDb>-90) {
        size_t first=std::max<size_t>(1,(size_t)ceilf(20*window/_sampleRate));
        size_t last=std::min(window/2-1,(size_t)floorf(_frequencyMax*window/_sampleRate));
        size_t best=first;float maximum=0;
        for(size_t k=first;k<=last;++k) {
            float v=magnitude(_real[k],_imag[k]);
            if(v>maximum) { maximum=v;best=k; }
        }
        _spectralMaximum=maximum;
        float a=logf(magnitude(_real[best-1],_imag[best-1])+1e-12f);
        float b=logf(maximum+1e-12f),c=logf(magnitude(_real[best+1],_imag[best+1])+1e-12f);
        float d=a-2*b+c,delta=fabsf(d)>1e-9f?0.5f*(a-c)/d:0;
        _result.spectralPeakHz=(best+std::max(-0.5f,std::min(0.5f,delta)))*_sampleRate/window;
    }
    for(size_t bar=0;bar<SPECTRUM_BARS;++bar) {
        float low=20*powf(_frequencyMax/20,(float)bar/SPECTRUM_BARS);
        float high=20*powf(_frequencyMax/20,(float)(bar+1)/SPECTRUM_BARS);
        size_t a=std::max<size_t>(1,(size_t)floorf(low*window/_sampleRate));
        size_t b=std::min(window/2,(size_t)ceilf(high*window/_sampleRate));
        float power=0;
        for(size_t k=a;k<=b;++k) power=std::max(power,magnitude(_real[k],_imag[k]));
        _spectrum[bar]=db(2*power/windowSum);
    }
    size_t first=std::max<size_t>(2,(size_t)(55*window/_sampleRate));
    size_t last=std::min(window/2-1,(size_t)(4000*window/_sampleRate));
    _tonalMaximum=_tonalNoise=0;
    for(size_t k=first;k<last;++k) {
        float value=magnitude(_real[k],_imag[k]);_tonalMaximum=std::max(_tonalMaximum,value);_tonalNoise+=value;
    }
    _tonalNoise/=std::max<size_t>(1,last-first);
    _result.tonality=_tonalNoise>1e-9f?_tonalMaximum/_tonalNoise:0;
    if(_result.rmsDb>-90) {
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
    size_t first=std::max<size_t>(1,(size_t)ceilf(center/ratio*_window/_sampleRate));
    size_t last=std::min(_window/2,(size_t)floorf(center*ratio*_window/_sampleRate));
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
    float frequency=(_sampleRate/4)/(selected+fraction);
    if(frequency<40 || frequency>2000) return;
    // Short periods make a quadratic YIN lag estimate biased at high notes.
    // Refine the already-selected fundamental with a log-Hann spectral peak.
    if(frequency>400) {
        size_t center=(size_t)lroundf(frequency*_window/_sampleRate),best=center;
        float maximum=0;
        for(size_t k=center>3?center-3:1;k<=center+3 && k+1<_window/2;++k) {
            float v=magnitude(_real[k],_imag[k]);if(v>maximum) { maximum=v;best=k; }
        }
        // A click elsewhere in the time window must not disable refinement of
        // a quiet tonal peak. Compare spectral evidence to spectral evidence.
        if(best>0 && best+1<_window/2 && maximum>_spectralMaximum*0.06f) {
            float a=logf(magnitude(_real[best-1],_imag[best-1])+1e-12f);
            float b=logf(maximum+1e-12f),c=logf(magnitude(_real[best+1],_imag[best+1])+1e-12f);
            float d=a-2*b+c;
            float delta=fabsf(d)>1e-9f ? 0.5f*(a-c)/d : 0;
            frequency=(best+std::max(-0.5f,std::min(0.5f,delta)))*_sampleRate/_window;
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
    float strongest=_tonalMaximum,noise=_tonalNoise;
    size_t first=std::max<size_t>(2,(size_t)(55*_window/_sampleRate));
    size_t last=std::min(_window/2-1,(size_t)(4000*_window/_sampleRate));
    for(size_t k=first;k<last && count<64;++k) {
        float a=magnitude(_real[k-1],_imag[k-1]),b=magnitude(_real[k],_imag[k]),c=magnitude(_real[k+1],_imag[k+1]);
        if(b<a || b<=c || b<std::max(strongest*0.02f,noise*4.0f)) continue;
        a=logf(a+1e-12f);c=logf(c+1e-12f);float center=logf(b+1e-12f),d=a-2*center+c;
        float delta=fabsf(d)>1e-9f ? 0.5f*(a-c)/d : 0;
        peaks[count++]={(k+std::max(-0.5f,std::min(0.5f,delta)))*_sampleRate/_window,b};
    }
    float chroma[12]={};
    int bass=-1;unsigned independent=0;
    for(size_t i=0;i<count;++i) {
        // Compress amplitude before harmonic weighting: room/mic response can
        // attenuate a genuine chord tone without changing its pitch class.
        float weight=sqrtf(peaks[i].amplitude/strongest);
        bool harmonic=false;
        for(size_t j=0;j<i;++j) {
            float ratio=peaks[i].frequency/peaks[j].frequency,nearest=roundf(ratio);
            if(nearest>=2 && nearest<=16 && fabsf(ratio-nearest)<0.018f*nearest) { weight*=0.22f;harmonic=true;break; }
        }
        float note=69+12*log2f(peaks[i].frequency/440.0f);
        int midi=(int)lroundf(note);
        if(fabsf(note-midi)>0.35f) continue;
        int pc=(midi%12+12)%12;
        if(!harmonic) independent|=1u<<pc;
        if(bass<0 && peaks[i].amplitude>strongest*0.06f) bass=pc;
        chroma[pc]+=weight;
    }
    unsigned independentCount=0;for(unsigned bits=independent;bits;bits>>=1) independentCount+=bits&1u;
    if(independentCount<3) return; // A single rich note is not a chord.
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
void AcousticDSP::waveformTrace(size_t span,float* values,size_t columns) const {
    if(!columns) return;
    span=std::min(span,_count);
    if(!span) { for(size_t x=0;x<columns;++x) values[x]=0;return; }
    float mean=0,peak=0;
    // Estimate DC over the available history, not a short visible fragment of
    // a cycle: subtracting that fragment's mean tilts the apparent centerline.
    for(size_t i=0;i<_count;++i) mean+=_samples[(_write+MAX_SAMPLES-_count+i)%MAX_SAMPLES]/32768.0f;
    mean/=_count;
    for(size_t i=0;i<span;++i) peak=std::max(peak,fabsf(_samples[(_write+MAX_SAMPLES-span+i)%MAX_SAMPLES]/32768.0f-mean));
    size_t lookback=std::min(span/2,_count-span),start=(_write+MAX_SAMPLES-span-lookback)%MAX_SAMPLES;
    size_t available=span+lookback;float offset=0;bool armed=false;
    float previous=_samples[start]/32768.0f-mean;
    for(size_t i=1;i<lookback;++i) {
        float value=_samples[(start+i)%MAX_SAMPLES]/32768.0f-mean;
        if(value<-peak*0.15f) armed=true;
        if(armed && previous<0 && value>=0) { offset=i-1-previous/(value-previous);break; }
        previous=value;
    }
    float width=std::max(1.0f,(float)span/columns);
    for(size_t x=0;x<columns;++x) {
        float position=offset+(columns>1?(float)x*(span-1)/(columns-1):0),value=0;
        if(width<=1) {
            size_t a=(size_t)position,b=std::min(available-1,a+1);float fraction=position-a;
            value=(_samples[(start+a)%MAX_SAMPLES]*(1-fraction)+_samples[(start+b)%MAX_SAMPLES]*fraction)/32768.0f;
        } else {
            float a=std::max(0.0f,position-width/2),b=std::min((float)available,position+width/2);
            for(size_t i=(size_t)a;i<(size_t)ceilf(b);++i) {
                float weight=std::min(b,(float)(i+1))-std::max(a,(float)i);
                value+=_samples[(start+i)%MAX_SAMPLES]*weight/32768.0f;
            }
            value/=b-a;
        }
        values[x]=value-mean;
    }
}
AcousticScale::AcousticScale() : _range(0.1f),_quietSince(0),_quiet(false),_initialized(false) {}
float AcousticScale::update(float peak,uint32_t now) {
    float wanted=std::max(1.0f/32768,peak/0.95f);
    if(!_initialized || wanted>_range) { _initialized=true;_range=wanted;_quiet=false; }
    else if(wanted<_range*0.92f) {
        if(!_quiet) { _quiet=true;_quietSince=now; }
        if(uint32_t(now-_quietSince)>=750) { _range=wanted;_quiet=false; }
    } else _quiet=false;
    return _range;
}
