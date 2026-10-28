#include "../../AcousticDSP.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

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
    if(!strcmp(argv[1],"pitch")) {
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
    } else if(!strcmp(argv[1],"adaptive-scale")) {
        AcousticScale scale;
        assert(scale.update(.4f,0)==.5f);
        assert(scale.update(.8f,10)==1);
        assert(scale.update(.01f,100)==1);
        assert(scale.update(.01f,1599)==1);
        assert(scale.update(.01f,1600)==.025f);
        assert(scale.update(.2f,1610)==.25f);
    } else return 2;
    puts("PASS");
}
