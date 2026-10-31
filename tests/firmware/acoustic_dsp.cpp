#include "../../AcousticDSP.h"
#include "../../AcousticPresentation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <fstream>
#include <cstdlib>

constexpr double PI=3.14159265358979323846;
void tone(AcousticDSP& dsp,const double* frequencies,int count,float amplitude=0.5f,size_t frames=8192) {
    dsp.resetInputHistory();
    std::vector<int16_t> data(frames);
    for(size_t i=0;i<frames;++i) {
        double v=0;for(int n=0;n<count;++n) v+=sin(2*PI*frequencies[n]*i/48000)*amplitude/count;
        data[i]=(int16_t)(v*32767);
    }
    dsp.push(data.data(),data.size());
}
void dumpNotes(const AcousticDSP::Result& r) {
    fprintf(stderr,"decision=%s bass=%d best=%d/%d %.3f runner=%d/%d %.3f\n",AcousticDSP::chordDecisionName(r.chordDecision),r.bassMidi,r.bestChord.root,r.bestChord.chord,r.bestChord.score,r.runnerUpChord.root,r.runnerUpChord.chord,r.runnerUpChord.score);
    for(unsigned n=0;n<r.noteCount;++n) fprintf(stderr," note=%d %.2fHz strength=%.3f observed=%d current=%d\n",r.notes[n].midi,r.notes[n].frequency,r.notes[n].strength,r.notes[n].observed,r.notes[n].current);
}
int main(int argc,char** argv) {
    if(argc==4 && (!strcmp(argv[1],"replay") || !strcmp(argv[1],"replay-notes"))) {
        // Opt-in local recording replay. Signed 16-bit little-endian mono PCM;
        // production chord mode, clock and display stabilizer, without RX/PA.
        unsigned rate=(unsigned)strtoul(argv[3],nullptr,10);
        auto replay=std::make_unique<AcousticDSP>(false);
        if(!replay->setSampleRate(rate)) return 2;
        std::ifstream input(argv[2],std::ios::binary);if(!input) return 2;
        replay->enableChordAnalysis(true);
        std::vector<int16_t> chunk(rate/10);AcousticTunerReading reading;
        uint64_t frames=0;
        while(input.read((char*)chunk.data(),chunk.size()*2) || input.gcount()) {
            size_t count=(size_t)input.gcount()/2;frames+=count;replay->push(chunk.data(),count);
            if(!replay->analyze(8192,false,true)) continue;
            reading.update(replay->result(),(uint32_t)(frames*1000/rate),true);
            const auto& raw=replay->result();const auto& shown=reading.result();
            if(!strcmp(argv[1],"replay-notes")) { fprintf(stderr,"time=%.3f\n",(double)frames/rate);dumpNotes(raw); }
            printf("%.3f,%.2f,%s%s,%s%s\n",(double)frames/rate,raw.rmsDb,
                raw.root<0?"--":AcousticDSP::noteName(raw.root),AcousticDSP::chordSuffix(raw.chord),
                shown.root<0?"--":AcousticDSP::noteName(shown.root),AcousticDSP::chordSuffix(shown.chord));
        }
        return input.eof()?0:2;
    }
    assert(argc==2);
    auto dsp=std::make_unique<AcousticDSP>();
    if(!strcmp(argv[1],"independent-fundamentals")) {
        const double f[]={130.812783,329.627557,783.990872};tone(*dsp,f,3,.6f);
        assert(dsp->analyze(8192,false,true));auto r=dsp->result();unsigned pcs=0;
        for(unsigned i=0;i<r.noteCount;++i) pcs|=1u<<(r.notes[i].midi%12);
        assert(pcs==((1u<<0)|(1u<<4)|(1u<<7)) && r.noteCount>=3);
    } else if(!strcmp(argv[1],"missing-fundamental-notes")) {
        const double f[]={220,330,440};tone(*dsp,f,3,.6f);assert(dsp->analyze(8192,false,true));
        auto r=dsp->result();dumpNotes(r);
        assert(r.noteCount==1 && r.notes[0].midi==45 && !r.notes[0].observed && r.chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"multi-resolution-bass")) {
        dsp->enableChordAnalysis(true);int16_t chunk[4800];
        for(int tick=0;tick<10;++tick) {
            for(int i=0;i<4800;++i) {
                double phase=2*PI*(tick*4800+i)/48000;
                chunk[i]=(int16_t)(32767*.15*(sin(82.406889*phase)+sin(130.812783*phase)+sin(195.997718*phase)));
            }
            dsp->push(chunk,4800);dsp->analyze(8192,false,true);
        }
        auto r=dsp->result();dumpNotes(r);
        assert(r.bassReady && fabs(r.bassBinHz-2.9296875f)<.001 && fabs(r.bassWindowMs-341.3333f)<.01);
        assert(r.bassMidi==40 && r.root==0 && r.chord==AcousticDSP::MAJOR);
        dsp->resetInputHistory();dsp->push(chunk,4800);assert(!dsp->analyze(8192,false,true));
    } else if(!strcmp(argv[1],"manual-reference-and-profiles")) {
        assert(dsp->configureTuning(442,NoteEstimator::KEYBOARD));
        assert(!dsp->configureTuning(399,NoteEstimator::GUITAR));
        double f=442;tone(*dsp,&f,1);assert(dsp->analyze(8192,true,false));auto r=dsp->result();
        assert(r.midi==69 && fabs(r.cents)<.5 && r.referenceHz==442);
        const double chord[]={442*pow(2.0,-9.0/12),442*pow(2.0,-5.0/12),442*pow(2.0,-2.0/12)};
        tone(*dsp,chord,3);dsp->analyze(8192,false,true);assert(dsp->result().root==0);
        assert(dsp->configureTuning(440,NoteEstimator::GUITAR));
        tone(*dsp,chord,3);dsp->analyze(8192,false,true);r=dsp->result();
        assert(r.root==0 && fabs(r.chordTuningCents-1200*log2(442.0/440))<1.5);
    } else if(!strcmp(argv[1],"expanded-vocabulary") || !strcmp(argv[1],"keyboard-vocabulary")) {
        if(!strcmp(argv[1],"keyboard-vocabulary")) assert(dsp->configureTuning(440,NoteEstimator::KEYBOARD));
        const int patterns[16][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},
            {0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1},{0,3,6,-1},{0,3,6,9},
            {0,3,6,10},{0,4,7,9},{0,3,7,9},{0,4,7,2},{0,3,7,2},{0,7,-1,-1}};
        for(int root=0;root<12;++root) for(unsigned q=0;q<16;++q) {
            double f[4];int count=0;
            for(int interval:patterns[q]) if(interval>=0) f[count++]=440*pow(2.0,(48+root+interval-69)/12.0);
            tone(*dsp,f,count,.6f,32768);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            if(r.root!=root || r.chord!=(int)q) { printf("vocabulary root=%d quality=%u -> %d/%d\n",root,q,r.root,r.chord);dumpNotes(r);return 1; }
        }
    } else if(!strcmp(argv[1],"slash-chords")) {
        const double f[]={164.813778,195.997718,261.625565};tone(*dsp,f,3,.6f);
        assert(dsp->analyze(8192,false,true));auto r=dsp->result();char label[32];dumpNotes(r);
        assert(r.root==0 && r.chord==AcousticDSP::MAJOR && r.bassMidi==52);
        AcousticDSP::formatChord(label,sizeof(label),r.root,r.chord,r.bassMidi);assert(!strcmp(label,"C/E"));
        AcousticDSP::formatChord(label,sizeof(label),9,AcousticDSP::MINOR,48);assert(!strcmp(label,"Am/C"));
    } else if(!strcmp(argv[1],"held-notes-cannot-create-chord")) {
        int16_t first[8192],next[9600];
        for(int i=0;i<8192;++i) first[i]=(int16_t)(12000*sin(2*PI*261.625565*i/48000));
        dsp->push(first,8192);assert(dsp->analyze(8192,false,true));
        for(int tick=0;tick<2;++tick) {
            for(int i=0;i<9600;++i) next[i]=(int16_t)(8*sin(2*PI*391.995436*(tick*9600+i)/48000));
            dsp->push(next,9600);assert(dsp->analyze(8192,false,true));
        }
        auto r=dsp->result();assert(r.chord==AcousticDSP::NONE);
        bool held=false;for(unsigned i=0;i<r.noteCount;++i) if(r.notes[i].midi%12==0 && !r.notes[i].current) held=true;
        assert(held); // This actually exercises retained feature evidence.
    } else if(!strcmp(argv[1],"sample-headroom-with-dc")) {
        int16_t samples[8192];
        for(int i=0;i<8192;++i) samples[i]=i<6144?32767:-32768;
        dsp->push(samples,8192);assert(dsp->analyze(8192,false,false));auto r=dsp->result();
        assert(r.atFullScale && fabs(r.peakDb)<.001 && r.peak>1.4f);
        // DC-centered scope amplitude may exceed one; input samples did not.
        for(auto& x:samples) x=16000;
        dsp->resetInputHistory();dsp->push(samples,8192);dsp->analyze(8192,false,false);r=dsp->result();
        assert(!r.atFullScale && fabs(r.peakDb-20*log10(16000.0/32768))<.001 && r.rmsDb<=-90);
        for(int i=0;i<8192;++i) samples[i]=i%2?32766:-32767;
        dsp->resetInputHistory();dsp->push(samples,8192);dsp->analyze(8192,false,false);
        assert(!dsp->result().atFullScale && dsp->result().peakDb<=0);
        std::vector<int16_t> square(32768+128);
        for(size_t i=0;i<square.size();++i) square[i]=i%480<240?31000:-31000;
        dsp->resetInputHistory();dsp->push(square.data(),square.size());
        assert(dsp->analyze(8192,false,false,true));
        assert(!dsp->result().atFullScale && fabs(dsp->result().peakDb-20*log10(31000.0/32768))<.001);
        // Analysis FIR overshoot must not manufacture an input FULL flag.
    } else if(!strcmp(argv[1],"bass-clock-and-inversions")) {
        for(uint32_t rate:{22050u,44100u,48000u}) {
            assert(dsp->setSampleRate(rate));dsp->enableChordAnalysis(true);
            std::vector<int16_t> samples(rate);
            for(int i=0;i<(int)rate;++i) samples[i]=(int16_t)(6000*(sin(2*PI*195.997718*i/rate)+sin(2*PI*261.625565*i/rate)+sin(2*PI*329.627557*i/rate)));
            dsp->push(samples.data(),samples.size());assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            assert(r.root==0 && r.chord==AcousticDSP::MAJOR && r.bassMidi==55);
            assert(r.bassReady && fabs(r.bassBinHz-rate/(4*4096.0))<.001);
            char label[32];AcousticDSP::formatChord(label,sizeof(label),r.root,r.chord,r.bassMidi);assert(!strcmp(label,"C/G"));
        }
        dsp->setSampleRate(48000);
        for(int root=0;root<12;++root) for(int minor=0;minor<2;++minor) {
            double f[]={440*pow(2.0,(51+!minor+root-69)/12.0),440*pow(2.0,(55+root-69)/12.0),440*pow(2.0,(60+root-69)/12.0)};
            tone(*dsp,f,3,.5f,32768);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            assert(r.root==root && r.chord==(minor?AcousticDSP::MINOR:AcousticDSP::MAJOR));
            assert(r.bassMidi==51+!minor+root);
        }
    } else if(!strcmp(argv[1],"analysis-memory-budget")) {
        printf("NoteEstimator=%zu AcousticDSP=%zu bytes\n",sizeof(NoteEstimator),sizeof(AcousticDSP));
        assert(sizeof(NoteEstimator)<=16*1024 && sizeof(AcousticDSP)<=128*1024);
    } else if(!strcmp(argv[1],"playback-sample-rates")) {
        int16_t data[8192];
        for(uint32_t rate:{22050u,44100u,48000u,96000u}) {
            assert(dsp->setSampleRate(rate));
            for(int i=0;i<8192;++i) data[i]=(int16_t)(12000*sin(2*PI*440*i/rate));
            dsp->resetInputHistory();dsp->push(data,8192);assert(dsp->analyze(8192,true,false));auto r=dsp->result();
            printf("rate=%u tuner=%.3f peak=%.3f\n",rate,r.frequency,r.spectralPeakHz);
            assert(fabs(r.frequency-440)<.3 && fabs(r.spectralPeakHz-440)<.3);
            assert(fabs(r.periodMs-1000.0/440)<.003);
        }
        assert(!dsp->setSampleRate(0));
    } else if(!strcmp(argv[1],"tuner-history-without-bass")) {
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
    } else if(!strcmp(argv[1],"quality-change-confirmation")) {
        AcousticTunerReading reading;AcousticDSP::Result r={};
        r.root=0;r.chord=AcousticDSP::MAJOR;r.chordConfidence=.9f;
        reading.update(r,100,true);reading.update(r,200,true);
        assert(reading.result().chord==AcousticDSP::MAJOR);
        r.chord=AcousticDSP::MAJOR7;r.chordConfidence=.7f;
        reading.update(r,300,true);reading.update(r,400,true);
        assert(reading.result().chord==AcousticDSP::MAJOR); // A brief extra partial is not a new quality.
        reading.update(r,500,true);assert(reading.result().chord==AcousticDSP::MAJOR7);
        r.chord=AcousticDSP::NONE;reading.update(r,1000,true);
        assert(reading.result().chord==AcousticDSP::NONE);
        reading.reset();r.chord=AcousticDSP::MAJOR;r.chordConfidence=.9f;
        reading.update(r,100,true);reading.update(r,200,true);
        r.chord=AcousticDSP::MAJOR7;r.chordConfidence=.55f;
        reading.update(r,300,true);reading.update(r,400,true);reading.update(r,500,true);
        assert(reading.result().chord==AcousticDSP::MAJOR);
        reading.update(r,600,true);assert(reading.result().chord==AcousticDSP::MAJOR7);
    } else if(!strcmp(argv[1],"auto-gain")) {
        AcousticAutoGain gain;AcousticDSP::Result r={};r.rmsDb=-120;r.peakDb=-120;
        assert(gain.update(r,18,2000)==18);
        r.rmsDb=-70;r.peakDb=-50;r.tonality=3;
        assert(gain.update(r,18,3000)==18);
        r.tonality=50;r.rmsDb=-60;r.peakDb=-50;
        assert(gain.update(r,18,3500)==18); // Tonal interference alone is insufficient.
        r.frequency=440;r.confidence=.99f;
        assert(gain.update(r,18,4000)==21);
        assert(gain.update(r,21,4500)==21);
        assert(gain.update(r,21,5000)==24);
        r.atFullScale=true;assert(gain.update(r,24,5250)==21);
        r.atFullScale=false;gain.manualOverride(5250);
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
        assert(dsp->result().atFullScale); // Filtering must not hide raw ADC full-scale samples.
    } else if(!strcmp(argv[1],"bass-playback-clock")) {
        dsp->setSampleRate(44100);std::vector<int16_t> data(32768+128);
        for(size_t i=0;i<data.size();++i) data[i]=(int16_t)(32767*.1*sin(2*PI*41.20344*i/44100));
        dsp->push(data.data(),data.size());assert(dsp->analyze(8192,false,false,true));
        assert(fabs(dsp->result().binHz-1.345825195)<.00001);
        assert(fabs(dsp->result().spectralPeakHz-41.20344)<.05);
    } else if(!strcmp(argv[1],"harmonic-note-rejection")) {
        int16_t data[8192];const int harmonics[]={1,2,3,4,5,7,9,11,13};
        for(int i=0;i<8192;++i) {
            double value=0;for(int harmonic:harmonics) value+=.2/harmonic*sin(2*PI*261.625565*harmonic*i/48000);
            data[i]=(int16_t)(value*32767);
        }
        dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,false,true);
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
    } else if(!strcmp(argv[1],"detuned-chords")) {
        const int patterns[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},
                                  {0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
        for(double cents:{-35.0,35.0}) for(int root=0;root<12;++root) for(int q=0;q<8;++q) {
            double f[4];int count=0;
            for(int interval:patterns[q]) if(interval>=0)
                f[count++]=440*pow(2.0,(60+root+interval-69+cents/100)/12);
            tone(*dsp,f,count,.5f);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            if(r.root!=root || r.chord!=q) {
                printf("detuned %.0fc root=%d quality=%d -> %d/%d\n",cents,root,q,r.root,r.chord);dumpNotes(r);return 1;
            }
        }
    } else if(!strcmp(argv[1],"silence-noise")) {
        int16_t data[8192]={};dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,true,true);
        assert(dsp->result().frequency==0 && dsp->result().chord==AcousticDSP::NONE);
        assert(dsp->result().spectralPeakHz==0);
        uint32_t state=1234;for(auto& x:data) {state=state*1664525u+1013904223u;x=(int16_t)((int)(state>>16)/5);}
        dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,true,true);
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
        dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,false,true);
        printf("uneven C major: chord=%d\n",dsp->result().chord);
        assert(dsp->result().root==0 && dsp->result().chord==AcousticDSP::MAJOR);
    } else if(!strcmp(argv[1],"weak-subharmonic-chord")) {
        // A quiet low rumble must not classify all genuine upper chord tones
        // as its harmonics simply because their frequencies are near multiples.
        int16_t data[8192];
        for(int i=0;i<8192;++i) data[i]=(int16_t)(32767*(.004*sin(2*PI*65.406391*i/48000)+.15*sin(2*PI*261.625565*i/48000)+.12*sin(2*PI*329.627557*i/48000)+.15*sin(2*PI*391.995436*i/48000)));
        dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,false,true);
        printf("C major + weak subharmonic: root=%d chord=%d\n",dsp->result().root,dsp->result().chord);
        assert(dsp->result().root==0 && dsp->result().chord==AcousticDSP::MAJOR);
    } else if(!strcmp(argv[1],"open-voicing-chord")) {
        const double voicings[][3]={{65.406391,82.406889,97.998859},{130.812783,329.627557,391.995436},{130.812783,164.813778,391.995436},{164.813778,195.997718,261.625565}};
        for(const auto& f:voicings) {
            tone(*dsp,f,3,.5f);dsp->analyze(8192,false,true);
            printf("open C major: root=%d chord=%d\n",dsp->result().root,dsp->result().chord);
            assert(dsp->result().root==0 && dsp->result().chord==AcousticDSP::MAJOR);
        }
    } else if(!strcmp(argv[1],"rich-chords")) {
        const int pattern[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
        int16_t data[8192];
        for(int root=0;root<12;++root) for(int quality=0;quality<8;++quality) {
            for(int i=0;i<8192;++i) {
                double v=0;
                for(int n=0;n<4;++n) if(pattern[quality][n]>=0) {
                    double f=440*pow(2.0,(60+root+pattern[quality][n]-69)/12.0);
                    for(int h=1;h<=8;++h) v+=.065/h*sin(2*PI*f*h*i/48000+n*.7+h*.2);
                }
                data[i]=(int16_t)(32767*v);
            }
            dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,false,true);auto r=dsp->result();
            if(r.root!=root || r.chord!=quality) {printf("FAIL rich root=%d quality=%d -> root=%d quality=%d\n",root,quality,r.root,r.chord);dumpNotes(r);return 1;}
        }
    } else if(!strcmp(argv[1],"bright-chord-missing-third")) {
        // A guitar/small-speaker response can favor high partials while its
        // third partial is absent. Those partials must not invent a seventh.
        const int patterns[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
        const int harmonics[]={1,2,4,5,6,8,10,12};
        const double levels[]={.02,.03,.03,.04,.015,.06,.08,.02};
        int16_t data[32768];
        for(int root=0;root<12;++root) for(int quality=0;quality<8;++quality) {
            for(int i=0;i<32768;++i) {
                double v=0;
                for(int n=0;n<4;++n) if(patterns[quality][n]>=0) {
                    double f=440*pow(2.0,(48+root+patterns[quality][n]-69)/12.0);
                    for(int h=0;h<8;++h) v+=levels[h]/2*sin(2*PI*f*harmonics[h]*i/48000+n*.7+h*.2);
                }
                data[i]=(int16_t)(32767*v);
            }
            dsp->resetInputHistory();dsp->push(data,32768);dsp->analyze(8192,false,true);auto r=dsp->result();
            if(r.root!=root || r.chord!=quality) {printf("FAIL bright root=%d quality=%d -> root=%d quality=%d\n",root,quality,r.root,r.chord);dumpNotes(r);return 1;}
        }
    } else if(!strcmp(argv[1],"open-wide-voicing")) {
        const int patterns[8][4]={{0,4,7,-1},{0,3,7,-1},{0,2,7,-1},{0,5,7,-1},
                                  {0,4,7,10},{0,4,7,11},{0,3,7,10},{0,4,8,-1}};
        // Root in octave 3, third/suspension in octave 5, fifth in octave 4.
        // In C major this is C3/E5/G4, where E5 is also near C3's fifth partial.
        for(int root=0;root<12;++root) for(int q=0;q<8;++q) {
            double f[4];int count=0;
            for(int n=0;n<4;++n) if(patterns[q][n]>=0) {
                int midi=48+root+patterns[q][n]+(n==1?24:n?12:0);
                f[count++]=440*pow(2.0,(midi-69)/12.0);
            }
            tone(*dsp,f,count,.5f);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            if(r.root!=root || r.chord!=q) {
                printf("wide root=%d quality=%d -> %d/%d\n",root,q,r.root,r.chord);dumpNotes(r);return 1;
            }
        }
    } else if(!strcmp(argv[1],"extreme-wide-voicing")) {
        for(int root=0;root<12;++root) {
            double scale=pow(2.0,root/12.0);
            const double f[]={130.812783*scale,659.255114*scale,1567.981744*scale}; // C3/E5/G6, transposed.
            tone(*dsp,f,3,.5f);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
            if(r.root!=root || r.chord!=AcousticDSP::MAJOR) {
                printf("extreme root=%d -> %d/%d\n",root,r.root,r.chord);dumpNotes(r);return 1;
            }
        }
    } else if(!strcmp(argv[1],"sparse-single-harmonic-note")) {
        for(int root=0;root<12;++root) {
            double base=130.812783*pow(2.0,root/12.0);
            const double f[]={base,base*5,base*12};
            tone(*dsp,f,3,.5f);assert(dsp->analyze(8192,false,true));
            assert(dsp->result().chord==AcousticDSP::NONE);
        }
    } else if(!strcmp(argv[1],"chord-decision-evidence")) {
        double f[]={261.625565,311.126984,329.627557,391.995436}; // Equal major/minor thirds.
        tone(*dsp,f,4,.5f);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
        assert(r.chord==AcousticDSP::NONE && r.chordDecision==AcousticDSP::AMBIGUOUS);
        assert(r.bestChord.chord!=AcousticDSP::NONE && r.runnerUpChord.chord!=AcousticDSP::NONE);
        assert(r.chordMargin>=0 && r.chordMargin<.025f);
        AcousticTunerReading reading;
        reading.update(r,100,true);reading.update(r,200,true);
        assert(reading.result().chord==AcousticDSP::NONE); // Do not confirm the best ambiguous candidate.
        double pair[]={261.625565,329.627557};tone(*dsp,pair,2);dsp->analyze(8192,false,true);
        assert(dsp->result().chordDecision==AcousticDSP::INSUFFICIENT);
        double triad[]={261.625565,329.627557,391.995436};tone(*dsp,triad,3);dsp->analyze(8192,false,true);
        assert(dsp->result().chordDecision==AcousticDSP::MATCH && dsp->result().bestChord.root==0);
        assert(!strcmp(AcousticDSP::chordDecisionName(dsp->result().chordDecision),"match"));
        reading.update(dsp->result(),300,true);reading.update(dsp->result(),400,true);
        reading.update(r,500,true);assert(reading.result().chord==AcousticDSP::MAJOR);
        reading.update(r,900,true);assert(reading.result().chord==AcousticDSP::NONE); // Bounded uncertain hold.
        double single=440;tone(*dsp,&single,1);dsp->analyze(8192,false,true);
        assert(dsp->result().chordDecision==AcousticDSP::NO_CHORD);
        int16_t silence[8192]={};dsp->push(silence,8192);dsp->analyze(8192,false,true);
        assert(dsp->result().chordDecision==AcousticDSP::NO_CHORD);
    } else if(!strcmp(argv[1],"progression-and-silence")) {
        AcousticTunerReading reading;int16_t chunk[4800];uint32_t now=0;
        const int roots[]={0,7,9,0},qualities[]={0,0,1,5};
        const int shapes[][4]={{0,4,7,-1},{0,3,7,-1},{0,4,7,11}};
        for(int segment=0;segment<4;++segment) {
            unsigned first=0;
            const int* shape=shapes[qualities[segment]==0?0:qualities[segment]==1?1:2];
            for(int tick=0;tick<12;++tick) {
                for(int i=0;i<4800;++i) {
                    double v=0;
                    for(int n=0;n<4;++n) if(shape[n]>=0) {
                        double f=440*pow(2.0,(48+roots[segment]+shape[n]-69)/12.0);
                        double phase=2*PI*f*(tick*4800+i)/48000;
                        v+=.09*sin(phase)+.025*sin(2*phase)+.012*sin(3*phase);
                    }
                    chunk[i]=(int16_t)(32767*v);
                }
                dsp->push(chunk,4800);now+=100;
                if(!dsp->analyze(8192,false,true)) continue;
                reading.update(dsp->result(),now,true);const auto& shown=reading.result();
                bool correct=shown.root==roots[segment] && shown.chord==qualities[segment];
                if(correct && !first) first=(tick+1)*100;
                if(tick>=5 && !correct) { fprintf(stderr,"segment=%d tick=%d shown=%d/%d\n",segment,tick,shown.root,shown.chord);dumpNotes(dsp->result());assert(correct); }
            }
            printf("synthetic transition root=%d quality=%d detected_ms=%u\n",roots[segment],qualities[segment],first);
            assert(first && first<=500);
        }
        for(int tick=0;tick<7;++tick) {
            for(auto& value:chunk) value=0;
            dsp->push(chunk,4800);now+=100;assert(dsp->analyze(8192,false,true));
            reading.update(dsp->result(),now,true);
        }
        assert(reading.result().chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"sparse-harmonic-chord")) {
        // Bright C/E/G voicing: missing low fundamentals and strong high
        // partials. G's fifth partial is B, not an independent seventh voice.
        const int eh[]={4,8,12,16,20},gh[]={1,5,10,15};
        const double ea[]={.369,.722,1,.618,.727},ga[]={.488,.408,.30,.033};
        int16_t data[8192];
        for(int i=0;i<8192;++i) {
            double v=.246*sin(2*PI*130.812783*4*i/48000);
            for(int h=0;h<5;++h) v+=ea[h]*sin(2*PI*164.813778*eh[h]*i/48000+.3*h);
            for(int h=0;h<4;++h) v+=ga[h]*sin(2*PI*195.997718*gh[h]*i/48000+.7*h);
            data[i]=(int16_t)(v*.12*32767);
        }
        dsp->resetInputHistory();dsp->push(data,8192);assert(dsp->analyze(8192,false,true));auto r=dsp->result();
        printf("sparse bright C major: root=%d quality=%d\n",r.root,r.chord);dumpNotes(r);
        assert(r.root==0 && r.chord==AcousticDSP::MAJOR);
    } else if(!strcmp(argv[1],"weak-fundamental-single-note")) {
        int16_t data[8192];
        for(int i=0;i<8192;++i) {
            double f=261.625565;double v=.005*sin(2*PI*f*i/48000);
            for(int h=2;h<=10;++h) v+=.24/h*sin(2*PI*f*h*i/48000);
            data[i]=(int16_t)(32767*v);
        }
        dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,false,true);
        assert(dsp->result().chord==AcousticDSP::NONE);
    } else if(!strcmp(argv[1],"pitch-with-transient")) {
        int16_t data[8192];
        for(int i=0;i<8192;++i) data[i]=(int16_t)(32767*(.005*sin(2*PI*440*i/48000)+.002*sin(2*PI*330*i/48000)));
        data[7000]=24000;dsp->resetInputHistory();dsp->push(data,8192);dsp->analyze(8192,true,false);
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
