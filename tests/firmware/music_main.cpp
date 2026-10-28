#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main(int argc, char** argv) {
    MusicApp app;
    if (!strcmp(argv[1], "terminal-release")) {
        app.stopAndReleaseMusic();
        CHECK(!player.loaded && manager.releases == 1);
        CHECK(app._mode == MusicApp::MODE_BROWSE);
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
