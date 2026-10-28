#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Canvas {
    std::vector<std::string> text;
    void fillScreen(int) {}
    void fillRect(int,int,int,int,int) {}
    void setFont(int) {}
    void setTextColor(int,int) {}
    void setCursor(int,int) {}
    void print(const char* value) { text.emplace_back(value); }
    void getTextBounds(const char* value,int,int,int16_t*,int16_t*,uint16_t* w,uint16_t* h) {
        *w = strlen(value)*5; *h = 7;
    }
};
struct Theme { int bg=0,headerBg=0,accent=1,muted=2; } theme;
const Theme& currentTheme() { return theme; }
int pokoClrWarn() { return 3; }
int pokoClrGreen() { return 4; }
int u8g2_font_helvB08_tf=0,u8g2_font_5x7_tf=0;
struct HeaderState {
    Canvas canvas;
    Canvas* _canvas=&canvas;
    enum { MODE_BROWSE, MODE_PLAYING };
    int _mode=MODE_BROWSE,_videoCount=0,_songCount=0,_catalogIndex=0,_catalogTotal=0;
    bool _serverError=false,_serverOffline=false,_contentMissing=false,_loadingList=false;
    bool _paused=false,_streamStarted=false;
};
struct VideoApp: HeaderState { void renderToCanvas(); };
struct MusicApp: HeaderState { void renderToCanvas(); };
#include "../../build/library-status-methods.inc"

template<class App> bool check(const char* state) {
    App app;
    std::string expected;
    if (!strcmp(state,"empty")) expected="0/0";
    else if (!strcmp(state,"offline")) { app._serverError=app._serverOffline=true; expected="Offline"; }
    else if (!strcmp(state,"error")) { app._serverError=true; expected="Error"; }
    else if (!strcmp(state,"missing")) { app._contentMissing=true; expected="Missing"; }
    else if (!strcmp(state,"loading")) { app._loadingList=true; expected="Loading..."; }
    else { app._songCount=app._videoCount=3;app._catalogTotal=3;app._catalogIndex=1;expected="2/3"; }
    app.renderToCanvas();
    if (app.canvas.text.size()!=2 || app.canvas.text.back()!=expected) {
        printf("FAIL: %s expected %s, got %s\n",state,expected.c_str(),app.canvas.text.back().c_str());return false;
    }
    return true;
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    if(!check<VideoApp>(argv[1]) || !check<MusicApp>(argv[1])) return 1;
    puts("PASS");return 0;
}
