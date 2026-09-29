#include <cstdio>
#include "TitleMarquee.h"
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main() {
    TitleMarquee marquee;
    marquee.reset(100);
    CHECK(!marquee.update(500, 100) && marquee.offset() == 0);
    marquee.reset(100);
    CHECK(!marquee.update(139, 200));
    CHECK(marquee.update(140, 200) && marquee.offset() == 1);
    CHECK(marquee.update(100 + 232 * 40, 200) && marquee.offset() == 0);
    CHECK(TitleMarquee::readingTime(100) == 2000);
    CHECK(TitleMarquee::readingTime(200) > 232 * 40);
    marquee.reset(UINT32_MAX - 19);
    CHECK(marquee.update(20, 200) && marquee.offset() == 1);
    puts("PASS");
}

