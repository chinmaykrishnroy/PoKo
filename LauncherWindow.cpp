#include "LauncherWindow.h"

uint8_t launcherWindowStart(uint8_t selected, uint8_t start,
                            uint8_t total, uint8_t visible) {
    if (!visible) return 0;
    if (selected >= total) selected = total ? total - 1 : 0;
    if (total <= visible) return 0;
    if (selected < start) start = selected;
    else if (selected >= start + visible) start = selected - visible + 1;
    return start > total - visible ? total - visible : start;
}
