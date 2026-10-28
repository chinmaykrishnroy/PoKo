#include "AcousticApp.h"
#include "AudioManager.h"
#include "MicrophoneInput.h"
#include "PokoTheme.h"
#include <U8g2lib.h>
#include <Preferences.h>
#include <algorithm>
#include <math.h>
#include <new>

namespace {
const unsigned WINDOWS[] = {1024,2048,4096,8192};
const unsigned TIME_MS[] = {2,5,10,20,50,100,160};
int plotY(float value) { return std::max(37,std::min(101,(int)lroundf(value))); }
}
AcousticApp::AcousticApp(Arduino_GFX* gfx,AppSwitchFn exitFn,bool tuner)
    : _gfx(gfx),_exit(exitFn),_tuner(tuner) {}
AcousticApp::~AcousticApp() { unload(); }
const char* AcousticApp::appName() const { return _tuner ? "Tuner" : "Analyzer"; }
bool AcousticApp::wantsHoldRepeat() const { return true; }
bool AcousticApp::isSafeForRestore() const { return false; }
void AcousticApp::begin() {
    Preferences p;p.begin(_tuner ? "tuner" : "analyzer",true);
    _mode=p.getUChar("mode",0)%(_tuner?2:4);
    _time=std::min<uint8_t>(6,p.getUChar("time",1));
    _fft=std::min<uint8_t>(4,p.getUChar("fft",3));
    _octave=p.getUChar("octave",0)%2;_response=p.getUChar("response",0)%2;
    int32_t savedGain=p.getInt("gain",18);
    _gain=(int)std::max<int32_t>(0,std::min<int32_t>(30,savedGain))/3*3;
    _preferredGain=_gain;
    p.end();
}
void AcousticApp::load() {
    if (_loaded) { renderToCanvas();return; }
    _loaded=true;_error=nullptr;_hasResult=false;_frames=0;_scale=AcousticScale();
    _spectrumRange=AcousticSpectrumRange();_reading.reset();_autoGain.reset(millis());_gain=_preferredGain;
    _range=0.1f;_meter=-120;_periodMs=0;_lastData=_lastAnalysis=_lastDraw=millis();
    _canvas=new(std::nothrow) Arduino_Canvas(128,128,_gfx);
    if (_canvas && !_canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) { delete _canvas;_canvas=nullptr; }
    _dsp=new(std::nothrow) AcousticDSP();
    if (!_canvas || !_dsp) _error="Not enough memory";
    else if (!audioManager || !audioManager->request(AUDIO_MIC)) _error="Microphone unavailable";
    else if (!microphoneInput.setGain(_gain)) _error="Microphone gain error";
    if (_error) {
        if(audioManager) audioManager->release(AUDIO_MIC);
        delete _dsp;_dsp=nullptr;
    }
    renderToCanvas();
}
void AcousticApp::unload() {
    if (!_loaded) return;
    if (audioManager) audioManager->release(AUDIO_MIC);
    if (_prefsDirty) {
        Preferences p;p.begin(_tuner ? "tuner" : "analyzer",false);
        p.putUChar("mode",_mode);p.putUChar("time",_time);p.putUChar("fft",_fft);
        p.putUChar("octave",_octave);p.putUChar("response",_response);p.putInt("gain",_preferredGain);p.end();
        _prefsDirty=false;
    }
    delete _canvas;_canvas=nullptr;delete _dsp;_dsp=nullptr;_loaded=false;_reading.reset();_hasResult=false;
}
bool AcousticApp::isCapturing() const {
    return _loaded && !_error && microphoneInput.running() && audioManager && audioManager->activeSource()==AUDIO_MIC;
}
AcousticApp::Status AcousticApp::status() const {
    Status state={};
    state.capturing=isCapturing();state.mode=_mode;state.frames=_frames;
    state.automaticGain=_tuner && _autoGain.automatic(millis());
    state.lastDataAgeMs=millis()-_lastData;state.error=_error;
    state.hasResult=state.capturing && _hasResult && state.lastDataAgeMs<500;
    if(state.hasResult && _dsp) state.signal=_dsp->result();
    else { state.signal.rmsDb=state.signal.peakDb=-120;state.signal.chord=AcousticDSP::NONE;state.signal.midi=-1; }
    state.displayed=_reading.result();
    return state;
}
void AcousticApp::update() {
    if (!_loaded) return;
    uint32_t now=millis();
    if (isCapturing() && _dsp) {
        int16_t samples[512];
        // Drain bounded DMA backlog before analysis; leave the service loop free.
        for (int i=0;i<8;++i) {
            size_t n=microphoneInput.read(samples,512);if(!n) break;
            _dsp->push(samples,n);_frames+=n;_lastData=now;
        }
        if (now-_lastAnalysis>=100) {
            uint32_t elapsed=now-_lastAnalysis;_lastAnalysis=now;
            if (now-_lastData<500) {
                unsigned window=(_tuner||_mode==0)?8192:WINDOWS[std::min<unsigned>(3,_fft)];
                _hasResult=_dsp->analyze(window,_tuner&&_mode==0,_tuner&&_mode==1,!_tuner&&_mode==1&&_fft==4);
                if (_hasResult) {
                    const auto& r=_dsp->result();
                    if(_tuner) {
                        _reading.update(r,now,true);
                        int target=_autoGain.update(r,_gain,now);
                        if(target!=_gain) {
                            if(microphoneInput.setGain(target)) _gain=target;
                            else { _error="Microphone gain error";_reading.reset();if(audioManager) audioManager->release(AUDIO_MIC); }
                        }
                    }
                    if (r.periodMs>0) {
                        float period=r.periodMs;
                        if (_periodMs<=0 || fabsf(period-_periodMs)>_periodMs*0.1f) _periodMs=period;
                        else _periodMs+=0.3f*(period-_periodMs);
                    } else _periodMs=0;
                    float alpha=1-expf(-(float)elapsed/(_response?700.0f:120.0f));
                    _meter+=alpha*(r.rmsDb-_meter);
                }
            } else _hasResult=false;
            if(!_hasResult) _reading.reset();
            _dirty=true;
        }
    } else if (!_error) { _error="Microphone stopped";_hasResult=false;_dirty=true; }
    if (_dirty && now-_lastDraw>=50) renderToCanvas();
}
void AcousticApp::text(int x,int y,const char* value,uint16_t color) {
    _canvas->setTextColor(color);_canvas->setCursor(x,y);_canvas->print(value);
}
void AcousticApp::renderToCanvas() {
    if (!_loaded) return;
    const auto& t=currentTheme();
    if (!_canvas) {
        _gfx->fillScreen(t.bg);_gfx->setFont(u8g2_font_5x7_tf);_gfx->setTextColor(t.text);
        _gfx->setCursor(4,55);_gfx->print("Not enough memory");return;
    }
    _canvas->fillScreen(t.bg);_canvas->setTextWrap(false);
    _canvas->fillRect(0,0,128,14,t.headerBg);_canvas->setFont(u8g2_font_helvB08_tf);
    text(3,11,appName(),t.accent);_canvas->setFont(u8g2_font_5x7_tf);
    char b[40];snprintf(b,sizeof(b),_tuner?(_autoGain.automatic(millis())?"AUTO %ddB":"MAN %ddB"):"MIC %ddB",_gain);
    int16_t gx,gy;uint16_t gw,gh;_canvas->getTextBounds(b,0,0,&gx,&gy,&gw,&gh);text(125-gw-gx,10,b,t.muted);
    if(_hasResult && _dsp->result().clipped) text(54,10,"CLIP",pokoClrErr());
    const char* modes[]={"Oscilloscope","Log FFT dBFS","Octave","Level meter"};
    text(4,25,_tuner?(_mode?"Chord  A4=440Hz":"Note   A4=440Hz"):modes[_mode],t.text);
    if (_error) { text(3,59,_error,t.accent);text(3,77,"2R: retry",t.muted); }
    else if (_tuner) drawTuner();
    else if (!_hasResult) text(5,64,millis()-_lastData>=500?"No microphone data":"Listening...",t.muted);
    else if (_mode==0) drawScope();
    else if (_mode==1 || _mode==2) drawSpectrum(_mode==2);
    else drawMeter();
    _canvas->fillRect(0,115,128,13,t.footerBg);_canvas->setFont(u8g2_font_5x7_tf);
    text(3,124,"2L:Back",t.footerText);
    int16_t x1,y1;uint16_t w,h;
    _canvas->getTextBounds("2R:Mode",0,0,&x1,&y1,&w,&h);
    text(125-w-x1,124,"2R:Mode",t.accent);
    _canvas->flush();_dirty=false;_lastDraw=millis();
}
void AcousticApp::drawScope() {
    const auto& t=currentTheme();float trace[118],peak=0;
    _dsp->waveformTrace(TIME_MS[_time]*48,trace,118);
    for(float value:trace) peak=std::max(peak,fabsf(value));
    _range=_scale.update(peak,millis());
    _canvas->drawRect(4,36,120,67,t.line);
    for(int y=52;y<=86;y+=17) _canvas->drawFastHLine(5,y,118,t.line);
    for(int x=34;x<123;x+=30) _canvas->drawFastVLine(x,37,65,t.line);
    uint16_t color=_dsp->result().clipped?pokoClrErr():pokoClrCyan();
    for(int x=1;x<118;++x) {
        _canvas->drawLine(x+4,plotY(69-trace[x-1]/_range*31),x+5,plotY(69-trace[x]/_range*31),color);
    }
    char b[40];_canvas->setFont(u8g2_font_5x7_tf);
    if(_range<0.01f) snprintf(b,sizeof(b),"%ums +/-%.1fmFS",TIME_MS[_time],_range*1000);
    else snprintf(b,sizeof(b),"%ums +/-%.3f FS",TIME_MS[_time],_range);
    text(4,34,b,t.muted);
    if(_periodMs>0) snprintf(b,sizeof(b),"Avg T %.2fms",_periodMs);
    else snprintf(b,sizeof(b),"Avg T -- (no period)");
    text(4,112,b,t.muted);
}
void AcousticApp::drawSpectrum(bool octave) {
    const auto& t=currentTheme();
    unsigned count=octave?(_octave?28:10):AcousticDSP::SPECTRUM_BARS;
    float maximum=-120;
    for(unsigned i=0;i<count;++i) maximum=std::max(maximum,octave?_dsp->octaveDb(i,_octave?3:1):_dsp->spectrum()[i]);
    float top=_spectrumRange.update(maximum,millis()),bottom=top-60;
    _canvas->drawRect(4,36,120,67,t.line);
    for(int y=58;y<102;y+=22) _canvas->drawFastHLine(5,y,118,t.line);
    for(unsigned i=0;i<count;++i) {
        float db=octave?_dsp->octaveDb(i,_octave?3:1):_dsp->spectrum()[i];
        int height=db<=-119?0:std::max(0,std::min(64,(int)lroundf((db-bottom)/60*64)));
        int x=5+i*118/count,next=5+(i+1)*118/count;
        if(height) _canvas->fillRect(x,102-height,std::max(1,next-x-(octave?1:0)),height,acousticBandColor(i,count,isDarkTheme()));
    }
    char b[40];_canvas->setFont(u8g2_font_5x7_tf);
    if(octave) { snprintf(b,sizeof(b),"%.0f..%.0f dBFS",bottom,top);text(4,34,b,t.muted); }
    else {
        float peakHz=_dsp->result().spectralPeakHz;
        if(peakHz>0) snprintf(b,sizeof(b),peakHz<1000?"Pk %.2fHz":"Pk %.0fHz",peakHz);
        else snprintf(b,sizeof(b),"Pk --Hz");
        text(4,34,b,t.text);
        snprintf(b,sizeof(b),"%.0f/%.0f",top,bottom);
        int16_t x1,y1;uint16_t w,h;_canvas->getTextBounds(b,0,0,&x1,&y1,&w,&h);text(125-w-x1,34,b,t.muted);
    }
    // Frequency labels follow the logarithmic x axis.
    text(4,111,octave?"31":"20",t.muted);
    text(37,111,octave?"250":(_fft==4?"100":"200"),t.muted);
    text(76,111,octave||_fft!=4?"2k":"500",t.muted);text(106,111,octave?"16k":(_fft==4?"2k":"20k"),t.muted);
    if(octave) text(84,25,_octave?"1/3 oct":"1 oct",t.muted);
    else {
        if(_fft==4) snprintf(b,sizeof(b),"Bass 1.46Hz");
        else snprintf(b,sizeof(b),"%u",WINDOWS[_fft]);
        text(_fft==4?70:98,25,b,t.muted);
    }
}
void AcousticApp::drawMeter() {
    const auto& t=currentTheme();const auto& r=_dsp->result();char b[40];
    _canvas->setFont(u8g2_font_helvB14_tf);snprintf(b,sizeof(b),"%.1f",_meter);text(7,57,b,t.accent);
    _canvas->setFont(u8g2_font_5x7_tf);text(81,56,"dBFS",t.muted);
    _canvas->drawRect(5,69,118,15,t.line);
    int width=std::max(0,std::min(114,(int)((_meter+60)/60*114)));
    for(int x=0;x<width;++x) _canvas->drawFastVLine(7+x,71,11,acousticLevelColor(-60+60.0f*x/114,isDarkTheme()));
    int peak=std::max(7,std::min(120,7+(int)((r.peakDb+60)/60*114)));
    _canvas->drawFastVLine(peak,70,13,t.text);
    snprintf(b,sizeof(b),"Peak %.1f %s",r.peakDb,r.clipped?"CLIP":"");text(5,97,b,r.clipped?pokoClrErr():t.text);
    text(5,111,_response?"Hold: Slow / Fast":"Hold: Fast / Slow",t.muted);
}
void AcousticApp::drawTuner() {
    const auto& t=currentTheme();const auto& r=_reading.result();char b[40];
    bool valid=_mode?r.chord!=AcousticDSP::NONE:r.frequency>0;
    uint16_t color=t.muted;
    if(valid) color=_mode?acousticBlend(isDarkTheme()?0xFD20:0xB360,pokoClrGreen(),std::max(0.0f,(r.chordConfidence-0.5f)*2)):
                         acousticTuningColor(r.cents,isDarkTheme());
    _canvas->setFont(u8g2_font_helvB18_tf);
    if(!valid) snprintf(b,sizeof(b),"--");
    else if(_mode) snprintf(b,sizeof(b),"%s%s",AcousticDSP::noteName(r.root),AcousticDSP::chordSuffix(r.chord));
    else snprintf(b,sizeof(b),"%s%d",AcousticDSP::noteName(r.midi),r.midi/12-1);
    int16_t x1,y1;uint16_t w,h;_canvas->getTextBounds(b,0,0,&x1,&y1,&w,&h);
    text(64-w/2-x1,57,b,color);_canvas->setFont(u8g2_font_5x7_tf);
    if(_mode) {
        if(valid) snprintf(b,sizeof(b),"Match %d%%",(int)(r.chordConfidence*100));
        else snprintf(b,sizeof(b),"Match --");
        text(7,78,b,color);_canvas->drawRect(6,86,116,7,t.line);
        if(valid) _canvas->fillRect(8,88,(int)(112*r.chordConfidence),3,color);
        snprintf(b,sizeof(b),"Input %.0fdBFS",_hasResult?_dsp->result().rmsDb:-120.0f);text(7,109,b,t.muted);
    } else {
        if(valid) snprintf(b,sizeof(b),r.frequency<100?"%.2f Hz  %+.1fc":"%.1f Hz  %+.1fc",r.frequency,r.cents);
        else snprintf(b,sizeof(b),"-- Hz  --c");
        text(6,77,b,color);
        _canvas->drawFastHLine(7,92,114,t.line);_canvas->drawFastVLine(64,87,11,t.text);
        if(valid) { int x=64+(int)(r.cents*1.1f);_canvas->fillCircle(x,92,3,color); }
        text(6,109,"-50    0    +50 cents",t.muted);
    }
}
void AcousticApp::refreshTheme() { _dirty=true;renderToCanvas(); }
void AcousticApp::adjustGain(int direction) {
    int gain=std::max(0,std::min(30,_gain+direction*3));
    if(microphoneInput.setGain(gain)) {
        _gain=_preferredGain=gain;_autoGain.manualOverride(millis());_prefsDirty=true;_dirty=true;
    }
    else {
        _error="Microphone gain error";_hasResult=false;_dirty=true;
        if(audioManager) audioManager->release(AUDIO_MIC);
    }
}
void AcousticApp::adjustParameter(int direction) {
    uint32_t now=millis();if(now-_lastEdit<300) return;_lastEdit=now;
    if(_tuner) { adjustGain(direction);return; }
    if(_mode==0) _time=std::max(0,std::min(6,(int)_time+direction));
    else if(_mode==1) _fft=std::max(0,std::min(4,(int)_fft+direction));
    else if(_mode==2) _octave=direction>0?1:0;
    else _response=direction>0?1:0;
    if(_mode==1 || _mode==2) _spectrumRange=AcousticSpectrumRange();
    if(_mode==1) _hasResult=false;
    _prefsDirty=true;_dirty=true;
}
void AcousticApp::onLeft() { adjustGain(-1); }
void AcousticApp::onRight() { adjustGain(1); }
void AcousticApp::onHoldingLeft() { adjustParameter(-1); }
void AcousticApp::onHoldingRight() { adjustParameter(1); }
void AcousticApp::onEnter() {
    if(_error) { unload();load();return; }
    _mode=(_mode+1)%(_tuner?2:4);_prefsDirty=true;_dirty=true;_hasResult=false;
    _reading.reset();_spectrumRange=AcousticSpectrumRange();
}
void AcousticApp::handleInput(InputEvent event) {
    switch(event) {
        case InputEvent::Left: onLeft();break;
        case InputEvent::Right: onRight();break;
        case InputEvent::LeftHolding: onHoldingLeft();break;
        case InputEvent::RightHolding: onHoldingRight();break;
        case InputEvent::Enter: case InputEvent::RightDouble: onEnter();break;
        case InputEvent::Back: case InputEvent::LeftDouble: if(_exit) _exit(STATE_LAUNCHER);break;
        default: break;
    }
}
