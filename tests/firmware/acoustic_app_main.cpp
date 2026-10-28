#include <cassert>
#include <cstring>
#include <fstream>
AppState exited=STATE_COUNT;
void exitApp(AppState state) { exited=state; }
int main(int argc,char** argv) {
    assert(argc==2);Arduino_GFX gfx;
    AcousticApp analyzer(&gfx,exitApp,false),tuner(&gfx,exitApp,true);
    if(!strcmp(argv[1],"overrun-clears-reading")) {
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
