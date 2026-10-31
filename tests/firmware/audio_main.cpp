#include <cstring>
uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
bool videoAudible = false;
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
    if (!strcmp(argv[1], "mic-from-music") || !strcmp(argv[1], "mic-from-video")) {
        static bool stopped=false;
        bool music=!strcmp(argv[1], "mic-from-music");
        if(music) manager.setMusicHandlers([](){stopped=true;},nullptr,nullptr,nullptr,nullptr,[](){return stopped;});
        else manager.setVideoHandlers([](){stopped=true;},[](){return stopped;});
        CHECK(manager.request(music?AUDIO_MUSIC:AUDIO_VIDEO));
        CHECK(manager.request(AUDIO_MIC));
        CHECK(stopped && microphoneInput.running() && manager.getPhysicalOwner()==AUDIO_MIC);
        manager.release(AUDIO_MIC);
        CHECK(manager.activeSource()==AUDIO_SSYNC && !snap.suspended && !microphoneInput.running());
    } else if (!strcmp(argv[1], "mic-resumes-incoming-ssync")) {
        CHECK(manager.request(AUDIO_MIC));
        CHECK(!manager.resumeIncomingPlayback(100));
        CHECK(manager.activeSource()==AUDIO_MIC && microphoneInput.running());
        snap.incoming=true;
        CHECK(manager.resumeIncomingPlayback(100));
        CHECK(manager.activeSource()==AUDIO_SSYNC && !microphoneInput.running() && !snap.suspended);
        CHECK(!manager.resumeIncomingPlayback(100));
    } else if (!strcmp(argv[1], "mic-handoff")) {
        CHECK(manager.request(AUDIO_MIC));
        CHECK(snap.suspended && microphoneInput.running());
        CHECK(manager.getPhysicalOwner()==AUDIO_MIC);
        CHECK(manager.suspendedSource()==AUDIO_SSYNC);
        CHECK(!manager.isSoundPlaying());
        manager.release(AUDIO_MIC);
        CHECK(!microphoneInput.running() && microphoneInput.stops==1);
        CHECK(manager.activeSource()==AUDIO_SSYNC && !snap.suspended);
    } else if (!strcmp(argv[1], "mic-failure-restores-ssync")) {
        microphoneInput.fail=true;
        CHECK(!manager.request(AUDIO_MIC));
        CHECK(!microphoneInput.running());
        CHECK(manager.activeSource()==AUDIO_SSYNC && !snap.suspended);
        CHECK(manager.getPhysicalOwner()==AUDIO_SSYNC);
    } else if (!strcmp(argv[1], "mic-stop-all")) {
        CHECK(manager.request(AUDIO_MIC));
        manager.stopAll();
        CHECK(!microphoneInput.running() && !snap.loaded);
        CHECK(manager.activeSource()==AUDIO_NONE && manager.getPhysicalOwner()==AUDIO_NONE);
    } else if (!strcmp(argv[1], "mic-handoff-timeout")) {
        snap.forceTimeout=true;
        CHECK(!manager.request(AUDIO_MIC));
        CHECK(!microphoneInput.running());
        CHECK(manager.activeSource()!=AUDIO_MIC);
    } else if (!strcmp(argv[1], "silent-video-is-not-audio")) {
        manager.setVideoHandlers(nullptr, nullptr, []() { return videoAudible; });
        CHECK(manager.request(AUDIO_VIDEO));
        CHECK(!manager.isSoundPlaying());
        videoAudible = true;
        CHECK(manager.isSoundPlaying());
        videoAudible = false;
        CHECK(!manager.isSoundPlaying());
    } else
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

