#include <cassert>
#include <cstring>
#include <fstream>
AppState exited=STATE_COUNT;
void exitApp(AppState state) { exited=state; }
int main(int argc,char** argv) {
    assert(argc==2);Arduino_GFX gfx;
    AcousticApp analyzer(&gfx,exitApp,false),tuner(&gfx,exitApp,true);
    if(!strcmp(argv[1],"playback-preserved")) {
        manager.source=AUDIO_MUSIC;manager.playing=true;speakerAmp=true;
        analyzer.load();
        assert(manager.requests==0 && manager.source==AUDIO_MUSIC);
        assert(!microphoneInput.running());
        analyzer.onEnter();analyzer.unload();assert(manager.releases==0 && manager.source==AUDIO_MUSIC);
        assert(preferenceWrites==0);analyzer.flushPreferences();assert(preferenceWrites==0);
        manager.playing=false;analyzer.flushPreferences();assert(preferenceWrites>0);
    } else if(!strcmp(argv[1],"playback-rate-and-transition")) {
        manager.source=AUDIO_MUSIC;manager.playing=true;speakerAmp=true;tuner.load();
        int16_t stereo[4410*2];
        for(int tick=0;tick<8;++tick) {
            for(int i=0;i<4410;++i) stereo[2*i]=stereo[2*i+1]=(int16_t)(12000*sin(6.28318530718*440*(tick*4410+i)/44100));
            playbackAudioTap.writeStereo(stereo,4410,44100);testMillis+=100;tuner.update();
        }
        auto state=tuner.status();assert(state.sampleRate==44100 && !strcmp(state.inputSource,"MUSIC"));
        assert(fabs(state.displayed.frequency-440)<.3 && !state.automaticGain && !microphoneInput.running());
        assert(gfx.text.find("MUSIC PCM")!=std::string::npos);
        tuner.onRight();assert(manager.source==AUDIO_MUSIC && manager.requests==0);
        manager.playing=false;speakerAmp=false;testMillis+=1000;tuner.update();
        assert(manager.source==AUDIO_MUSIC && !microphoneInput.running() && !tuner.status().hasResult);
        manager.source=AUDIO_NONE;testMillis+=100;tuner.update();
        assert(manager.source==AUDIO_MIC && !strcmp(tuner.status().inputSource,"MIC"));
        assert(tuner.status().sampleRate==48000 && microphoneInput.running());
        manager.incoming=true;testMillis+=100;tuner.update();
        assert(manager.source==AUDIO_SSYNC && !microphoneInput.running());
        assert(!strcmp(tuner.status().inputSource,"SSYNC")); // Allow time for output to prime.
        tuner.unload();assert(manager.source==AUDIO_SSYNC);
    } else if(!strcmp(argv[1],"music-starts-during-microphone")) {
        analyzer.load();assert(microphoneInput.running() && manager.requests==1);
        manager.source=AUDIO_MUSIC;manager.playing=true;microphoneInput.active=false;speakerAmp=true;
        testMillis+=100;analyzer.update();
        assert(!strcmp(analyzer.status().inputSource,"MUSIC") && analyzer.isCapturing());
        assert(manager.requests==1 && manager.source==AUDIO_MUSIC);
        analyzer.unload();assert(manager.source==AUDIO_MUSIC && manager.releases==0);
    } else if(!strcmp(argv[1],"overrun-clears-reading")) {
        tuner.load();for(int i=0;i<3;++i){testMillis+=100;tuner.update();}
        assert(tuner.status().displayed.frequency>0);
        microphoneInput.noData=true;++microphoneInput.overflowCount;testMillis+=100;tuner.update();
        assert(!tuner.status().hasResult && tuner.status().displayed.frequency==0);
        microphoneInput.noData=false;testMillis+=100;tuner.update();assert(tuner.status().hasResult);
        microphoneInput.overflowOnRead=microphoneInput.reads+3;testMillis+=100;tuner.update();
        assert(!tuner.status().hasResult && tuner.status().displayed.frequency==0);
    } else if(!strcmp(argv[1],"auto-gain-persistence")) {
        microphoneInput.amplitude=.0012f;tuner.begin();tuner.load();
        for(int i=0;i<22;++i) {testMillis+=100;tuner.update();}
        assert(microphoneInput.gainDb>18 && tuner.status().automaticGain);
        tuner.onEnter();tuner.unload();assert(storedPrefs["tunergain"]==18);
        tuner.load();assert(microphoneInput.gainDb==18);
        tuner.onRight();assert(microphoneInput.gainDb==21 && !tuner.status().automaticGain);
        tuner.unload();assert(storedPrefs["tunergain"]==21);
    } else if(!strcmp(argv[1],"render-preview")) {
        auto save=[&](const char* name){std::ofstream out(std::string("build/host-tests/")+name+".draw");for(const auto& command:gfx.commands) out<<command<<'\n';};
        auto tick=[&](){for(int i=0;i<8;++i){testMillis+=100;analyzer.update();tuner.update();}};
        analyzer.load();tick();save("scope");analyzer.onEnter();tick();save("fft");analyzer.unload();
        microphoneInput.quiet=true;tuner.load();tick();save("tuner-idle");microphoneInput.quiet=false;tick();save("tuner-a440");
        microphoneInput.frequency=430;tick();save("tuner-flat");microphoneInput.chord=true;tuner.onEnter();tick();save("tuner-chord");tuner.unload();
    } else if(!strcmp(argv[1],"right-footer-and-fft-headroom")) {
        analyzer.load();for(int i=0;i<4;++i) {testMillis+=100;analyzer.update();}
        analyzer.onEnter();testMillis+=100;analyzer.update();
        bool found=false;for(const auto& label:gfx.labels) if(label.value=="2R:Mode") {
            assert(label.x+35==125 && label.y==124);found=true;
        }
        assert(found);int tallest=0;
        for(const auto& rect:gfx.rects) if(rect.x>=5 && rect.y>=37 && rect.y+rect.h==102) tallest=std::max(tallest,rect.h);
        assert(tallest>=50 && tallest<=52); // 80% of 64 px, not 95%.
    } else if(!strcmp(argv[1],"quiet-placeholder")) {
        microphoneInput.quiet=true;tuner.load();
        for(int i=0;i<4;++i) {testMillis+=100;tuner.update();}
        assert(gfx.text.find("--")!=std::string::npos);
        assert(gfx.text.find("Play a steady note")==std::string::npos);
        tuner.onEnter();testMillis+=100;tuner.update();
        assert(gfx.text.find("--")!=std::string::npos);
        assert(gfx.text.find("No clear chord")==std::string::npos);
    } else if(!strcmp(argv[1],"scope-connected-trace")) {
        analyzer.load();for(int i=0;i<4;++i) {testMillis+=100;analyzer.update();}
        assert(gfx.lines.size()>=117);
        for(const auto& l:gfx.lines) assert(l.x1==l.x0+1);
    } else if(!strcmp(argv[1],"modes-and-holds")) {
        analyzer.begin();analyzer.load();assert(analyzer.isCapturing() && gfx.begins==0);
        assert(gfx.text.find("Listening")!=std::string::npos);
        for(int i=0;i<4;++i) { testMillis+=100;analyzer.update(); }
        assert(analyzer.status().frames==32768 && analyzer.status().capturing && analyzer.status().hasResult);
        assert(gfx.text.find("Oscilloscope")!=std::string::npos);
        assert(gfx.text.find("Avg T 2.27ms")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingRight();analyzer.renderToCanvas();
        assert(gfx.text.find("10ms")!=std::string::npos);
        analyzer.onEnter();testMillis+=100;analyzer.update();assert(gfx.text.find("Log FFT")!=std::string::npos);
        assert(gfx.text.find("Pk 440.")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingLeft();testMillis+=100;analyzer.update();
        assert(gfx.text.find("4096")!=std::string::npos);
        analyzer.onEnter();testMillis+=100;analyzer.update();assert(gfx.text.find("Octave")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingRight();analyzer.renderToCanvas();assert(gfx.text.find("1/3 oct")!=std::string::npos);
        analyzer.onEnter();testMillis+=100;analyzer.update();assert(gfx.text.find("Level meter")!=std::string::npos);
        analyzer.onEnter();testMillis+=100;analyzer.update();assert(gfx.text.find("Oscilloscope")!=std::string::npos);
        assert(preferenceWrites==0);analyzer.unload();assert(preferenceWrites>0);
        assert(liveCanvases==0 && !microphoneInput.running());
    } else if(!strcmp(argv[1],"tuner-and-stale-input")) {
        tuner.begin();tuner.load();
        for(int i=0;i<4;++i) { testMillis+=100;tuner.update(); }
        assert(gfx.text.find("A4")!=std::string::npos && gfx.text.find("440.0 Hz")!=std::string::npos);
        microphoneInput.noData=true;testMillis+=1000;tuner.update();
        assert(gfx.text.find("-- Hz")!=std::string::npos);
        assert(!tuner.status().hasResult && tuner.status().signal.frequency==0);
        assert(gfx.text.find("440.0 Hz")==std::string::npos);
        microphoneInput.noData=false;tuner.onEnter();testMillis+=100;tuner.update();
        assert(gfx.text.find("Chord")!=std::string::npos && gfx.text.find("Match --")!=std::string::npos);
        tuner.handleInput(InputEvent::LeftDouble);assert(exited==STATE_LAUNCHER);
        tuner.unload();assert(!microphoneInput.running());
    } else if(!strcmp(argv[1],"failure-and-retry")) {
        manager.fail=true;analyzer.load();assert(!analyzer.isCapturing());
        assert(gfx.text.find("Microphone unavailable")!=std::string::npos);
        manager.fail=false;analyzer.onEnter();assert(analyzer.isCapturing());
        analyzer.unload();assert(liveCanvases==0 && manager.releases==1);
        failCanvasBegin=true;analyzer.load();assert(!analyzer.isCapturing() && liveCanvases==0);
        assert(gfx.text.find("Not enough memory")!=std::string::npos);analyzer.unload();
        failCanvasBegin=false;analyzer.load();microphoneInput.failGain=true;analyzer.onRight();analyzer.renderToCanvas();
        assert(!analyzer.isCapturing() && gfx.text.find("Microphone gain error")!=std::string::npos);
        microphoneInput.failGain=false;analyzer.onEnter();assert(analyzer.isCapturing());analyzer.unload();
    } else if(!strcmp(argv[1],"external-stop-and-reload")) {
        analyzer.load();manager.release(AUDIO_MIC);testMillis+=100;analyzer.update();
        assert(!analyzer.isCapturing() && gfx.text.find("Microphone stopped")!=std::string::npos);
        analyzer.onEnter();assert(analyzer.isCapturing());
        analyzer.unload();tuner.load();assert(tuner.isCapturing() && liveCanvases==1);
        tuner.unload();assert(liveCanvases==0 && !microphoneInput.running());
    } else return 2;
    puts("PASS");
}
