#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <sstream>
constexpr int GFX_SKIP_OUTPUT_BEGIN=-2;
inline bool failCanvasBegin=false;
inline int liveCanvases=0;
class Arduino_GFX {
public:
    virtual ~Arduino_GFX()=default;
    std::string text;
    struct Line {int x0,y0,x1,y1;};
    std::vector<Line> lines;
    struct Label {int x,y;std::string value;uint16_t color;};
    std::vector<Label> labels;
    struct Rect {int x,y,w,h;uint16_t color;};
    std::vector<Rect> rects;
    std::vector<std::string> commands;
    int cursorX=0,cursorY=0,fontSize=5;
    uint16_t textColor=0;
    int begins=0,flushes=0;
    virtual bool begin(int=0) { ++begins;return true; }
    void fillScreen(uint16_t color) { text.clear();lines.clear();labels.clear();rects.clear();commands.clear();record("clear",0,0,128,128,color); }
    void record(const char* op,int x,int y,int w,int h,uint16_t color) {
        std::ostringstream out;out<<op<<' '<<x<<' '<<y<<' '<<w<<' '<<h<<' '<<color;commands.push_back(out.str());
    }
    void drawLine(int x0,int y0,int x1,int y1,uint16_t color) { lines.push_back({x0,y0,x1,y1});record("line",x0,y0,x1,y1,color); }
    void setTextWrap(bool) {}
    void fillRect(int x,int y,int w,int h,uint16_t color) {rects.push_back({x,y,w,h,color});record("fill",x,y,w,h,color);}
    void drawRect(int x,int y,int w,int h,uint16_t color) {record("rect",x,y,w,h,color);}
    void drawFastHLine(int x,int y,int w,uint16_t color) {record("line",x,y,x+w-1,y,color);}
    void drawFastVLine(int x,int y,int h,uint16_t color) {record("line",x,y,x,y+h-1,color);}
    void fillCircle(int x,int y,int r,uint16_t color) {record("circle",x,y,r,0,color);}
    void setFont(const uint8_t* font) { fontSize=*font; }
    void getTextBounds(const char* value,int,int,int16_t* x,int16_t* y,uint16_t* w,uint16_t* h) {
        *x=0;*y=0;*w=(uint16_t)(std::string(value).size()*(fontSize==18?13:5));*h=fontSize;
    }
    void setTextColor(uint16_t color) {textColor=color;}
    void setCursor(int x,int y) {cursorX=x;cursorY=y;}
    void print(const char* value) {
        text+=value;text+='\n';labels.push_back({cursorX,cursorY,value,textColor});
        std::ostringstream out;out<<"text "<<cursorX<<' '<<cursorY<<' '<<fontSize<<' '<<textColor<<' '<<value;commands.push_back(out.str());
    }
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
    void flush() { _output->text=text;_output->lines=lines;_output->labels=labels;_output->rects=rects;_output->commands=commands;++_output->flushes; }
};
