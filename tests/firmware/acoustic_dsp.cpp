#include "../../AcousticDSP.h"
#include "../../AcousticPresentation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

constexpr double PI=3.14159265358979323846;
void tone(AcousticDSP& dsp,const double* frequencies,int count,float amplitude=0.5f) {
    int16_t data[8192];
    for(int i=0;i<8192;++i) {
        double v=0;for(int n=0;n<count;++n) v+=sin(2*PI*frequencies[n]*i/48000)*amplitude/count;
        data[i]=(int16_t)(v*32767);
    }
    dsp.push(data,8192);
}
int main(int argc,char** argv) {
    assert(argc==2);
    auto dsp=std::make_unique<AcousticDSP>();
    if(!strcmp(argv[1],"tuner-history-without-bass")) {
        auto tuner=std::make_unique<AcousticDSP>(false);double f=440;
        for(int i=0;i<5;++i) tone(*tuner,&f,1);
        assert(!tuner->analyze(8192,false,false,true));
        assert(tuner->analyze(8192,true,false));assert(fabs(tuner->result().frequency-440)<1);
        tuner->resetInputHistory();int16_t fragment[512]={};tuner->push(fragment,512);
        assert(!tuner->analyze(8192,true,false));
    } else if(!strcmp(argv[1],"scope-trace-math")) {
        std::vector<int16_t> data(16384);
        for(size_t i=0;i<data.size();++i) data[i]=(int16_t)(32767*(.2+.5*sin(2*PI*440*i/48000)));
        dsp->push(data.data(),data.size());float trace[118];dsp->waveformTrace(240,trace,118);
        float low=1,high=-1;for(float value:trace){low=std::fmin(low,value);high=std::fmax(high,value);}
        assert(fabs((high+low)/2)<.02 && high>.48 && low<-.48);
    } else if(!strcmp(argv[1],"fft-headroom")) {
        AcousticSpectrumRange range;float top=range.update(-40,0);
        assert(fabs((-40-(top-60))/60-.8f)<.001f);
        assert(range.update(-35,100)==top); // Spend headroom without resizing.
        top=range.update(-27,200);
        assert(fabs((-27-(top-60))/60-.8f)<.001f);
        assert(range.update(-120,300)==top); // No full-height digital silence.
    } else if(!strcmp(argv[1],"reading-hysteresis")) {
        AcousticTunerReading reading;AcousticDSP::Result r={};
        r.frequency=440;r.confidence=.99f;r.root=0;r.chord=AcousticDSP::MAJOR;r.chordConfidence=.9f;
        reading.update(r,100,true);assert(reading.result().chord==AcousticDSP::NONE);
        auto gap=r;gap.frequency=0;gap.chord=AcousticDSP::NONE;
        reading.update(gap,200,true);reading.update(r,300,true);
        assert(reading.result().chord==AcousticDSP::MAJOR);
        reading.update(gap,400,true);assert(reading.result().chord==AcousticDSP::MAJOR);
        reading.update(gap,751,true);assert(reading.result().chord==AcousticDSP::NONE && reading.result().frequency==0);
        r.frequency=523.25f;reading.update(r,800,true);assert(fabs(reading.result().frequency-523.25f)<.01f);
        reading.update(r,900,false);assert(reading.result().frequency==0 && reading.result().chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"auto-gain")) {
        AcousticAutoGain gain;AcousticDSP::Result r={};r.rmsDb=-120;r.peakDb=-120;
        assert(gain.update(r,18,2000)==18);
        r.rmsDb=-70;r.peakDb=-50;r.tonality=3;
        assert(gain.update(r,18,3000)==18);
        r.tonality=50;r.rmsDb=-60;r.peakDb=-50;
        assert(gain.update(r,18,4000)==21);
        assert(gain.update(r,21,4500)==21);
        assert(gain.update(r,21,5000)==24);
        r.clipped=true;assert(gain.update(r,24,5250)==21);
        r.clipped=false;gain.manualOverride(5250);
        assert(gain.update(r,21,7000)==21 && !gain.automatic(7000));
        assert(gain.update(r,21,10250)==24);
    } else if(!strcmp(argv[1],"semantic-colors")) {
        assert(acousticTuningColor(0,true)==0x07E0);
        assert(acousticTuningColor(50,true)==0xF800);
        assert(acousticTuningColor(8,true)==acousticTuningColor(-8,true));
        assert(acousticTuningColor(8,true)!=acousticTuningColor(0,true));
        assert(acousticBandColor(0,96,true)!=acousticBandColor(95,96,true));
        assert(acousticLevelColor(-30,true)!=acousticLevelColor(-3,true));
    } else if(!strcmp(argv[1],"bass-resolution")) {
        std::vector<int16_t> data(32768+128);
        for(size_t i=0;i<data.size();++i) data[i]=(int16_t)(32767*.1*sin(2*PI*41.20344*i/48000));
        dsp->push(data.data(),data.size());assert(dsp->analyze(8192,false,false,true));
        assert(fabs(dsp->result().binHz-1.46484375)<.0001);
        assert(fabs(dsp->result().spectralPeakHz-41.20344)<.05);
        assert(dsp->result().maxHz==2000);
        for(size_t i=0;i<data.size();++i) data[i]=(int16_t)(32767*(.01*sin(2*PI*55*i/48000)+.7*sin(2*PI*10000*i/48000)));
        dsp->push(data.data(),data.size());dsp->analyze(8192,false,false,true);
        assert(fabs(dsp->result().spectralPeakHz-55)<.05); // Anti-alias filter.
        data.back()=32767;dsp->push(data.data(),data.size());dsp->analyze(8192,false,false,true);
        assert(dsp->result().clipped); // Filtering must not hide ADC clipping.
    } else if(!strcmp(argv[1],"harmonic-note-rejection")) {
        int16_t data[8192];const int harmonics[]={1,2,3,4,5,7,9,11,13};
        for(int i=0;i<8192;++i) {
            double value=0;for(int harmonic:harmonics) value+=.2/harmonic*sin(2*PI*261.625565*harmonic*i/48000);
            data[i]=(int16_t)(value*32767);
        }
        dsp->push(data,8192);dsp->analyze(8192,false,true);
        assert(dsp->result().chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"pitch")) {
        const double tones[]={41.20344,82.40689,110,220,440,443,880,1760};
        for(double f:tones) {
            tone(*dsp,&f,1);
            assert(dsp->analyze(8192,true,false));
            auto r=dsp->result();
            float error=(float)(1200*log2(r.frequency/f));
            if(!r.frequency || fabs(error)>4) { printf("FAIL pitch %.3f -> %.3f (%.2f cents)\n",f,r.frequency,error);return 1; }
            assert(r.confidence>0.9f);
        }
        double a=440;tone(*dsp,&a,1);dsp->analyze(4096,true,false);
        assert(dsp->result().midi==69 && fabs(dsp->result().cents)<1);
    } else if(!strcmp(argv[1],"spectrum-levels")) {
        double f=500;tone(*dsp,&f,1);assert(dsp->analyze(8192,false,false));
        assert(fabs(dsp->result().rmsDb+9.03f)<0.1f);
        assert(fabs(dsp->result().peakDb+6.02f)<0.1f);
        assert(fabs(dsp->result().spectralPeakHz-500)<0.2f);
        assert(fabs(dsp->octaveDb(4,1)+9.03f)<0.2f);
        float peak=-120;for(unsigned i=0;i<AcousticDSP::SPECTRUM_BARS;++i) peak=std::fmax(peak,dsp->spectrum()[i]);
        assert(peak>-7.5f && peak<-5.9f);
        int16_t low[118],high[118];dsp->waveform(4800,low,high,118);
        for(int i=0;i<118;++i) assert(low[i]<=high[i]);
        int16_t mixed[8192];
        for(int i=0;i<8192;++i) mixed[i]=(int16_t)(32767*(0.15*sin(2*PI*220*i/48000)+0.6*sin(2*PI*1500*i/48000)));
        dsp->push(mixed,8192);dsp->analyze(8192,false,false);
        assert(fabs(dsp->result().spectralPeakHz-1500)<0.2f);
    } else if(!strcmp(argv[1],"chords")) {
        const int pattern[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
        for(int root=0;root<12;++root) for(int quality=0;quality<8;++quality) {
            double f[4];int notes=0;
            for(int interval:pattern[quality]) if(interval>=0) f[notes++]=440*pow(2.0,(60+root+interval-69)/12.0);
            tone(*dsp,f,notes,0.8f);assert(dsp->analyze(8192,false,true));
            auto r=dsp->result();
            if(r.root!=root || r.chord!=quality) {printf("FAIL root=%d chord=%d -> root=%d chord=%d confidence=%.2f\n",root,quality,r.root,r.chord,r.chordConfidence);return 1;}
        }
    } else if(!strcmp(argv[1],"silence-noise")) {
        int16_t data[8192]={};dsp->push(data,8192);dsp->analyze(8192,true,true);
        assert(dsp->result().frequency==0 && dsp->result().chord==AcousticDSP::NONE);
        assert(dsp->result().spectralPeakHz==0);
        uint32_t state=1234;for(auto& x:data) {state=state*1664525u+1013904223u;x=(int16_t)((int)(state>>16)/5);}
        dsp->push(data,8192);dsp->analyze(8192,true,true);
        assert(dsp->result().frequency==0 && dsp->result().chord==AcousticDSP::NONE);
        assert(dsp->result().periodMs==0);
        double f=440;tone(*dsp,&f,1);dsp->analyze(8192,false,true);
        assert(dsp->result().chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"scope-period")) {
        const double frequencies[]={41.20344,440,4000,12000};
        for(double f:frequencies) {
            tone(*dsp,&f,1);dsp->analyze(8192,false,false);
            assert(fabs(dsp->result().periodMs-1000/f)<0.002f);
        }
        int16_t square[8192];for(int i=0;i<8192;++i) square[i]=(i%96)<48?12000:-12000;
        dsp->push(square,8192);dsp->analyze(8192,false,false);
        assert(fabs(dsp->result().periodMs-2)<0.001f);
    } else if(!strcmp(argv[1],"weak-chord")) {
        double f[]={261.625565,329.627557,391.995436};
        tone(*dsp,f,3,0.0012f);dsp->analyze(8192,false,true);
        printf("quiet C major: chord=%d rms=%.1f\n",dsp->result().chord,dsp->result().rmsDb);
        assert(dsp->result().root==0 && dsp->result().chord==AcousticDSP::MAJOR);
    } else if(!strcmp(argv[1],"uneven-chord")) {
        int16_t data[8192];
        for(int i=0;i<8192;++i) data[i]=(int16_t)(32767*(.012*sin(2*PI*261.625565*i/48000)+.16*sin(2*PI*329.627557*i/48000)+.10*sin(2*PI*391.995436*i/48000)));
        dsp->push(data,8192);dsp->analyze(8192,false,true);
        printf("uneven C major: chord=%d\n",dsp->result().chord);
        assert(dsp->result().root==0 && dsp->result().chord==AcousticDSP::MAJOR);
    } else if(!strcmp(argv[1],"pitch-with-transient")) {
        int16_t data[8192];
        for(int i=0;i<8192;++i) data[i]=(int16_t)(32767*(.005*sin(2*PI*440*i/48000)+.002*sin(2*PI*330*i/48000)));
        data[7000]=24000;dsp->push(data,8192);dsp->analyze(8192,true,false);
        auto r=dsp->result();printf("440Hz + interference/transient: tuner=%.2f FFT=%.2f\n",r.frequency,r.spectralPeakHz);
        assert(fabs(r.spectralPeakHz-440)<1 && fabs(r.frequency-440)<1);
    } else if(!strcmp(argv[1],"scale-headroom")) {
        AcousticScale scale;scale.update(.001f,0);float range=scale.update(.001f,2000);
        printf("quiet scope peak occupancy %.1f%%\n",100*.001f/range);
        assert(.001f/range>.93f && .001f/range<.97f);
    } else if(!strcmp(argv[1],"adaptive-scale")) {
        AcousticScale scale;
        float large=scale.update(.8f,10);
        assert(fabs(.8f/large-.95f)<.001f);
        assert(scale.update(.01f,100)==large);
        assert(scale.update(.01f,849)==large);
        assert(fabs(.01f/scale.update(.01f,850)-.95f)<.001f);
        assert(fabs(.2f/scale.update(.2f,860)-.95f)<.001f);
    } else return 2;
    puts("PASS");
}
