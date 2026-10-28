#include "../../LauncherWindow.h"
#include <cassert>
int main() {
    uint8_t start=0;
    for(uint8_t i=0;i<10;++i) {
        start=launcherWindowStart(i,start,10);
        assert(start==(i<8?0:i-7));
        assert(i>=start && i<start+8);
    }
    for(int i=9;i>=0;--i) {
        start=launcherWindowStart(i,start,10);
        assert(start==(i>=2?2:i));
    }
    assert(launcherWindowStart(9,0,10)==2);
    assert(launcherWindowStart(0,2,10)==0);
    assert(launcherWindowStart(7,2,8)==0);
    assert(launcherWindowStart(0,0,0)==0);
}
