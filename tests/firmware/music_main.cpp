#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main(int argc, char** argv) {
    MusicApp app;
    if (!strcmp(argv[1], "remote-metadata")) {
        RemotePlayback media{"chosen","Chosen song","Chosen artist",177,12,17,40};
        CHECK(app.prepareRemoteMedia(media));
        CHECK(app._mode==MusicApp::MODE_PLAYING && !app._paused && !app._streamStarted);
        CHECK(!strcmp(app._songs[0].id,"chosen") && !strcmp(app._songs[0].title,"Chosen song"));
        CHECK(app._songs[0].duration_s==177 && app._trackPos==12);
        CHECK(app._catalogIndex==17 && app._catalogTotal==40 && app._selectedIdx==0 && app._songCount==1);
        CHECK(!app._artBitmapValid && app._artSize==0 && app._loadedId[0]==0);
        CHECK(app.artworkId=="chosen");
    } else if (!strcmp(argv[1], "remote-listener-failure")) {
        app.listenerReady=false;
        RemotePlayback media{"chosen","Chosen song"};
        CHECK(!app.prepareRemoteMedia(media));
        CHECK(!strcmp(app._songs[0].id,"old") && app.artworkFetches==0);
    } else if (!strcmp(argv[1], "server-handoff-no-stop")) {
        app._serverTransition=true;
        app.requestStop();
        CHECK(serverStops==0);
    } else if (!strcmp(argv[1], "terminal-release")) {
        app.stopAndReleaseMusic();
        CHECK(!player.loaded && manager.releases == 1);
        CHECK(app._mode == MusicApp::MODE_BROWSE);
    } else if (!strcmp(argv[1], "handoff-defers-release")) {
        app.stopAndReleaseMusic(false);
        CHECK(!player.loaded && manager.releases == 0);
    } else if (!strcmp(argv[1], "handoff-timeout-retries")) {
        player.timeout = true;
        app.stopAndReleaseMusic(false);
        CHECK(app._cleanupPending && manager.releases == 0);
        player.timeout = false;
        app.update();
        CHECK(!app._cleanupPending && manager.releases == 1);
    } else if (!strcmp(argv[1], "shutdown-timeout-retains-owner")) {
        player.timeout = true;
        app.stopAndReleaseMusic();
        CHECK(app._cleanupPending && manager.releases == 0);
        player.timeout = false;
        app.update();
        CHECK(!app._cleanupPending && manager.releases == 1);
    } else if (!strcmp(argv[1], "startup-timeout-releases")) {
        app._streamStarted = false;
        testMillis = 11000;
        app.update();
        CHECK(manager.releases == 1 && app._serverError);
    } else if (!strcmp(argv[1], "blocked-loop-clock")) {
        testMillis = 3600;
        app.update();
        CHECK(app._trackPos == 3);
        testMillis = 8700;
        app.update();
        CHECK(app._trackPos == 8);
    }
    puts("PASS");
}
