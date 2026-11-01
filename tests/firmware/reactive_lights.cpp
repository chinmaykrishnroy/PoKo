#include "../../ReactiveLights.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

constexpr double PI=3.141592653589793;
ReactiveLightInput::Frame tone(float frequency,uint32_t rate,bool spectrum,bool opposite=false) {
    ReactiveLightInput dsp;ReactiveLightInput::Frame frame;
    std::vector<int16_t> pcm((rate/100)*2);
    for(unsigned tick=0;tick<100;++tick) {
        for(unsigned i=0;i<rate/100;++i) {
            int16_t x=(int16_t)(10000*sin(2*PI*frequency*(tick*(rate/100)+i)/rate));
            pcm[i*2]=x;pcm[i*2+1]=opposite?-x:x;
        }
        auto original=pcm;
        frame=dsp.process(pcm.data(),pcm.size()/2,rate,spectrum,tick*10);
        assert(pcm==original); // Analysis never modifies the playback block.
    }
    return frame;
}
int main(int argc,char** argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"frequency-bands-and-clocks")) {
        const float centers[]={63,125,250,500,1000,2000,4000,8000};
        for(uint32_t rate:{22050u,44100u,48000u,96000u}) for(unsigned band=0;band<8;++band) {
            auto f=tone(centers[band],rate,true);unsigned best=0;
            for(unsigned i=0;i<8;++i) if(f.bands[i]>f.bands[best]) best=i;
            if(best!=band) {printf("rate=%u tone=%.0f expected=%u actual=%u\n",rate,centers[band],band,best);return 1;}
        }
    } else if(!strcmp(argv[1],"prism-groups-stereo-and-dc")) {
        for(unsigned group=0;group<3;++group) {
            auto f=tone(group==0?80:group==1?1000:8000,48000,false);
            unsigned best=0;for(unsigned i=1;i<3;++i) if(f.groups[i]>f.groups[best]) best=i;
            assert(best==group);
            auto anti=tone(group==0?80:group==1?1000:8000,48000,false,true);
            assert(fabsf(anti.groups[group]-f.groups[group])<.00001f);
        }
        ReactiveLightInput dsp;int16_t dc[960];for(auto& x:dc)x=12000;
        ReactiveLightInput::Frame f;
        for(unsigned i=0;i<100;++i) f=dsp.process(dc,480,48000,true,i*10);
        for(float x:f.groups) assert(x<.00001f);
        for(float x:f.bands) assert(x<.00001f);
    } else if(!strcmp(argv[1],"gap-rate-and-mode-reset")) {
        ReactiveLightInput dsp;int16_t pcm[960];for(unsigned i=0;i<960;++i)pcm[i]=i%48<24?16000:-16000;
        dsp.process(pcm,480,48000,true,10);for(auto& x:pcm)x=0;
        auto f=dsp.process(pcm,480,48000,true,1000);
        for(float x:f.bands) assert(x==0);
        f=dsp.process(pcm,480,44100,false,1010);for(float x:f.groups) assert(x==0);
        f=dsp.process(nullptr,480,48000,true,1020);for(float x:f.bands) assert(x==0);
    } else if(!strcmp(argv[1],"pattern-focus-mask-volume-and-silence")) {
        ReactiveLightPattern pattern;ReactiveLightPattern::RGB out[8];
        ReactiveLightInput::Frame f;for(auto& x:f.groups)x=.2f;for(auto& x:f.bands)x=.2f;
        pattern.render(f,false,3,1,70,255,20,out);
        bool colorful=false;for(unsigned i=0;i<8;++i) if(out[i].b>out[i].r || out[i].g>out[i].r) colorful=true;
        assert(colorful);
        pattern.render(f,false,3,0,70,255,40,out);for(auto x:out)assert(x.r+x.g+x.b==0);
        pattern.render(f,false,3,1,70,5,60,out);for(unsigned i=0;i<8;++i)if(!(5&(1<<i)))assert(out[i].r+out[i].g+out[i].b==0);
        for(unsigned response=0;response<3;++response) {
            pattern=ReactiveLightPattern();pattern.render(f,true,response,1,70,255,20,out);
            for(unsigned i=0;i<8;++i) {
                bool enabled=response==0?i<3:response==1?i>=3&&i<6:i>=6;
                assert((out[i].r+out[i].g+out[i].b>0)==enabled);
            }
        }
        f={};for(unsigned i=0;i<500;++i)pattern.render(f,false,3,1,70,255,100+i*20,out);
        for(auto x:out)assert(x.r+x.g+x.b==0);
        for(auto& x:f.groups)x=.0007f;
        pattern=ReactiveLightPattern();pattern.render(f,false,3,1,70,255,20,out);
        for(auto x:out)assert(x.r+x.g+x.b==0);
        assert(sizeof(ReactiveLightInput)+sizeof(ReactiveLightPattern)<2048);
    } else if(!strcmp(argv[1],"motion-and-filter-isolation")) {
        ReactiveLightInput::Frame f;f.groups[0]=.3f;
        ReactiveLightPattern p;ReactiveLightPattern::RGB first[8],last[8];
        p.render(f,false,0,1,70,255,20,first);
        for(unsigned i=1;i<100;++i)p.render(f,false,0,1,70,255,20+i*20,last);
        assert(memcmp(first,last,sizeof(first))!=0);
        p=ReactiveLightPattern();f={};f.groups[2]=.3f;
        for(unsigned i=0;i<30;++i)p.render(f,false,0,1,70,255,20+i*20,last);
        for(auto x:last)assert(x.r+x.g+x.b==0);
    } else return 2;
    puts("PASS");
}
