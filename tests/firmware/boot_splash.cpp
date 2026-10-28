#include "splash_stubs/Arduino_GFX_Library.h"
#include "../../BootSplash.h"
#include <cassert>
#include <cstdio>
#include "splash_stubs/Arduino.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    Arduino_GFX display;
    if (!strcmp(argv[1], "white-first")) {
        BootSplash splash;
        assert(splash.begin(&display, 1234));
        assert(display.text == "PoKo" && display.color == 0xffff);
        for (auto pixel : display.pixels) assert(pixel == 0 || pixel == 0xffff);
        assert(display.beginCalls == 0 && display.flushes == 1);
        assert(liveCanvases == 1);
        splash.finish();
        assert(liveCanvases == 0);
        for (auto pixel : display.pixels) assert(pixel == 0 || pixel == 0xffff);
    } else if (!strcmp(argv[1], "fusion-randomized")) {
        BootSplash splash;
        splash.begin(&display, 1234);
        splash.update(115);
        auto first = display.pixels;
        int colored = 0;
        for (auto pixel : first) colored += pixel != 0 && pixel != 0xffff;
        assert(colored > 500 && display.color == 0xffff && display.text == "PoKo");
        splash.update(60);
        assert(display.pixels == first); // Out-of-order milestones cannot rewind.
        splash.finish();
        splash.update(175);
        assert(liveCanvases == 0);
        splash.begin(&display, 9876);
        splash.update(115);
        assert(display.pixels != first && display.beginCalls == 0);
    } else if (!strcmp(argv[1], "nonblocking-finish")) {
        BootSplash splash;
        splash.begin(&display,1234);
        uint32_t before=splashClock;
        splash.finish();
        if(splashClock!=before) { puts("FAIL: splash completion blocks connectivity servicing");return 1; }
        assert(liveCanvases==0);
    } else if (!strcmp(argv[1], "visible-duration")) {
        BootSplash splash;
        splash.begin(&display,1234);
        int networkPolls=0;
        for(uint32_t elapsed=0;elapsed<1100;elapsed+=10) {
            splashClock=elapsed;
            assert(splash.tick());
            assert(splashClock==elapsed); // No service time consumed by waits.
            ++networkPolls;
        }
        splashClock=1100;
        assert(!splash.tick());
        assert(networkPolls==110 && display.flushes>=20 && liveCanvases==0);
    } else if (!strcmp(argv[1], "slow-startup")) {
        BootSplash splash;
        splash.begin(&display,1234);
        splashClock+=2000;
        assert(!splash.tick());
        assert(splashClock==2000 && liveCanvases==0);
    } else if (!strcmp(argv[1], "clock-wrap")) {
        splashClock=0xfffffff0U;
        uint32_t started=splashClock;
        BootSplash splash;
        splash.begin(&display,1234);
        for(unsigned elapsed=0;elapsed<1100;elapsed+=10) { assert(splash.tick());splashClock+=10; }
        assert(!splash.tick());
        assert(uint32_t(splashClock-started)==1100);
        assert(liveCanvases==0);
    } else if (!strcmp(argv[1], "allocation-fallback")) {
        failCanvasBegin = true;
        BootSplash splash;
        assert(splash.begin(&display, 1234));
        assert(liveCanvases == 0 && display.text == "PoKo" && display.color == 0xffff);
        splash.update(115);
        splash.finish();
        assert(display.beginCalls == 0);
        assert(!splash.begin(nullptr, 1));
    } else return 2;
    puts("PASS");
}
