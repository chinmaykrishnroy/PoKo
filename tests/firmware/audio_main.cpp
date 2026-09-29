#include <cstring>
uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main(int argc, char** argv) {
    SnapPlayer snap;
    AudioManager manager;
    manager.setSnapPlayer(&snap);
    if (!strcmp(argv[1], "stop-empty")) {
        manager.stopAll();
        CHECK(manager.activeSource() == AUDIO_NONE);
        CHECK(manager.getPhysicalOwner() == AUDIO_NONE);
        CHECK(outputReleased == 0);
        puts("PASS");
        return 0;
    }
    CHECK(manager.request(AUDIO_SSYNC));
    if (!strcmp(argv[1], "suspend-transition")) {
        CHECK(manager.request(AUDIO_VIDEO));
        CHECK(snap.waitTimeouts == 0);
        CHECK(manager.activeSource() == AUDIO_VIDEO);
        CHECK(manager.suspendedSource() == AUDIO_SSYNC);
        CHECK(manager.getPhysicalOwner() == AUDIO_VIDEO);
        manager.release(AUDIO_VIDEO);
        CHECK(manager.activeSource() == AUDIO_SSYNC);
        CHECK(outputReleased >= 2);
    } else if (!strcmp(argv[1], "suspend-timeout")) {
        snap.forceTimeout = true;
        CHECK(!manager.request(AUDIO_MUSIC));
        CHECK(manager.getPhysicalOwner() != AUDIO_MUSIC);
    } else if (!strcmp(argv[1], "stop-all")) {
        manager.stopAll();
        CHECK(snap.waitTimeouts == 0);
        CHECK(manager.activeSource() == AUDIO_NONE);
        CHECK(manager.getPhysicalOwner() == AUDIO_NONE);
    }
    puts("PASS");
}

