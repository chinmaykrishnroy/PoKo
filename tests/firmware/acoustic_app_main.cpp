#include <cassert>
#include <cstring>
AppState exited=STATE_COUNT;
void exitApp(AppState state) { exited=state; }
int main(int argc,char** argv) {
    assert(argc==2);Arduino_GFX gfx;
    AcousticApp analyzer(&gfx,exitApp,false),tuner(&gfx,exitApp,true);
    if(!strcmp(argv[1],"modes-and-holds")) {
        analyzer.begin();analyzer.load();assert(analyzer.isCapturing() && gfx.begins==0);
        assert(gfx.text.find("Listening")!=std::string::npos);
        for(int i=0;i<4;++i) { testMillis+=100;analyzer.update(); }
        assert(analyzer.status().frames==16384 && analyzer.status().capturing && analyzer.status().hasResult);
        assert(gfx.text.find("Oscilloscope")!=std::string::npos);
        assert(gfx.text.find("Avg T 2.27ms")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingRight();analyzer.renderToCanvas();
        assert(gfx.text.find("50ms")!=std::string::npos);
        analyzer.onEnter();analyzer.renderToCanvas();assert(gfx.text.find("Log FFT")!=std::string::npos);
        assert(gfx.text.find("Pk 440Hz")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingLeft();analyzer.renderToCanvas();
        assert(gfx.text.find("4096")!=std::string::npos);
        analyzer.onEnter();analyzer.renderToCanvas();assert(gfx.text.find("Octave")!=std::string::npos);
        testMillis+=400;analyzer.onHoldingRight();analyzer.renderToCanvas();assert(gfx.text.find("1/3 oct")!=std::string::npos);
        analyzer.onEnter();analyzer.renderToCanvas();assert(gfx.text.find("Level meter")!=std::string::npos);
        analyzer.onEnter();analyzer.renderToCanvas();assert(gfx.text.find("Oscilloscope")!=std::string::npos);
        assert(preferenceWrites==0);analyzer.unload();assert(preferenceWrites>0);
        assert(liveCanvases==0 && !microphoneInput.running());
    } else if(!strcmp(argv[1],"tuner-and-stale-input")) {
        tuner.begin();tuner.load();
        for(int i=0;i<4;++i) { testMillis+=100;tuner.update(); }
        assert(gfx.text.find("A4")!=std::string::npos && gfx.text.find("440.0 Hz")!=std::string::npos);
        microphoneInput.noData=true;testMillis+=1000;tuner.update();
        assert(gfx.text.find("No microphone data")!=std::string::npos);
        assert(!tuner.status().hasResult && tuner.status().signal.frequency==0);
        assert(gfx.text.find("440.0 Hz")==std::string::npos);
        microphoneInput.noData=false;tuner.onEnter();testMillis+=100;tuner.update();
        assert(gfx.text.find("Chord")!=std::string::npos && gfx.text.find("No clear chord")!=std::string::npos);
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
