#include "../../PlaybackAudioTap.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>

int main(int argc,char** argv) {
    assert(argc==2);
    PlaybackAudioTap tap;
    int16_t stereo[512*2], mono[512];
    for(int i=0;i<512;++i) { stereo[2*i]=1000;stereo[2*i+1]=-1000; }
    uint32_t rate=0,generation=0;
    if(!strcmp(argv[1],"disabled-and-copy")) {
        tap.writeStereo(stereo,512,44100);assert(tap.read(mono,512,rate,generation)==0);
        tap.enable(true);tap.writeStereo(stereo,512,44100);
        assert(tap.read(mono,512,rate,generation)==512 && rate==44100);
        for(int16_t value:mono) assert(value==1000); // Anti-phase stereo survives.
        tap.enable(false);tap.writeStereo(stereo,512,44100);
        assert(tap.read(mono,512,rate,generation)==0);
    } else if(!strcmp(argv[1],"overflow-and-rate-change")) {
        tap.enable(true);
        for(int i=0;i<20;++i) tap.writeStereo(stereo,512,48000);
        assert(tap.overruns()>0);
        size_t count=0,n;while((n=tap.read(mono,512,rate,generation)))count+=n;
        assert(count==8192);uint32_t previous=generation;
        tap.writeStereo(stereo,512,44100);
        assert(tap.read(mono,512,rate,generation)==512 && rate==44100 && generation!=previous);
    } else if(!strcmp(argv[1],"pause-discards-history")) {
        tap.enable(true);tap.writeStereo(stereo,512,48000,100);
        tap.read(mono,0,rate,generation);uint32_t previous=generation;
        for(int i=0;i<1024;++i) stereo[i]=2000;
        tap.writeStereo(stereo,512,48000,1000);
        assert(tap.read(mono,512,rate,generation)==512 && generation!=previous);
        for(int16_t value:mono) assert(value==2000);
        assert(tap.read(mono,512,rate,generation)==0);
    } else if(!strcmp(argv[1],"silent-channel-handoff")) {
        tap.enable(true);tap.writeStereo(stereo,512,48000);
        tap.read(mono,512,rate,generation);uint32_t previous=generation;
        for(int i=0;i<512;++i) { stereo[2*i]=0;stereo[2*i+1]=1200; }
        for(int i=0;i<30;++i) {tap.writeStereo(stereo,512,48000);tap.read(mono,512,rate,generation);}
        assert(generation!=previous);for(int16_t value:mono) assert(value==1200);
    } else if(!strcmp(argv[1],"producer-consumer")) {
        tap.enable(true);std::atomic<bool> done{false};
        std::thread producer([&]{for(int i=0;i<2000;++i) tap.writeStereo(stereo,512,48000);done.store(true);});
        size_t count=0;
        while(!done.load()) {
            size_t n=tap.read(mono,512,rate,generation);count+=n;
            for(size_t i=0;i<n;++i) assert(mono[i]==1000);
        }
        producer.join();count+=tap.read(mono,512,rate,generation);assert(count>0);
    } else return 2;
    puts("PASS");
}
