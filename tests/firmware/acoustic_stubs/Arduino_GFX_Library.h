#pragma once
#include <string>
#include <cstdint>
constexpr int GFX_SKIP_OUTPUT_BEGIN=-2;
inline bool failCanvasBegin=false;
inline int liveCanvases=0;
class Arduino_GFX {
public:
    virtual ~Arduino_GFX()=default;
    std::string text;
    int begins=0,flushes=0;
    virtual bool begin(int=0) { ++begins;return true; }
    void fillScreen(uint16_t) { text.clear(); }
    void setTextWrap(bool) {}
    void fillRect(int,int,int,int,uint16_t) {}
    void drawRect(int,int,int,int,uint16_t) {}
    void drawFastHLine(int,int,int,uint16_t) {}
    void drawFastVLine(int,int,int,uint16_t) {}
    void fillCircle(int,int,int,uint16_t) {}
    void setFont(const uint8_t*) {}
    void setTextColor(uint16_t) {}
    void setCursor(int,int) {}
    void print(const char* value) { text+=value;text+='\n'; }
};
class Arduino_Canvas:public Arduino_GFX {
    Arduino_GFX* _output;
public:
    Arduino_Canvas(int,int,Arduino_GFX* output):_output(output) { ++liveCanvases; }
    ~Arduino_Canvas() { --liveCanvases; }
    bool begin(int mode=0) override {
        if(mode!=GFX_SKIP_OUTPUT_BEGIN) _output->begin();
        return !failCanvasBegin;
    }
    void flush() { _output->text=text;++_output->flushes; }
};
