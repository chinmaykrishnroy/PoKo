uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main(int argc, char** argv) {
    VideoApp app;
    if (!strcmp(argv[1], "next")) {
        app.onRight();
        CHECK(app._catalogIndex == 1 && app._selectedIdx == 1);
        CHECK(player.unloads == 1 && player.loads == 1);
        CHECK(player.resetsWhileRunning == 0);
        CHECK(app.thumbnails == 0);
        CHECK(requests == 1 && listRequests == 0 && app._mode == VideoApp::MODE_PLAYING);
    } else if (!strcmp(argv[1], "next-page")) {
        app._catalogIndex = 31;
        app._catalogTotal = 60;
        app.onRight();
        CHECK(app._catalogIndex == 32 && app._selectedIdx == 0);
        CHECK(listRequests == 1 && requests == 1 && app._mode == VideoApp::MODE_PLAYING);
    } else if (!strcmp(argv[1], "page-failure")) {
        app._catalogIndex = 31;
        app._catalogTotal = 60;
        listResult = false;
        app.onRight();
        CHECK(app._catalogIndex == 31 && requests == 0 && player.unloads == 0);
        CHECK(app._mode == VideoApp::MODE_PLAYING);
    } else if (!strcmp(argv[1], "http-failure")) {
        httpResult = 500;
        app.onRight();
        CHECK(app._mode == VideoApp::MODE_BROWSE && app._serverError);
        CHECK(power.locks == 0 && audio.releases == 1);
        CHECK(!player.loaded);
    } else if (!strcmp(argv[1], "stop-timeout")) {
        player.failUnload = true;
        app.requestStop();
        CHECK(audio.releases == 0 && player.loaded);
    } else if (!strcmp(argv[1], "next-stop-timeout")) {
        player.failUnload = true;
        app.onRight();
        CHECK(requests == 0 && player.loads == 0 && audio.releases == 0);
        CHECK(app._mode == VideoApp::MODE_BROWSE && app._serverError);
    } else if (!strcmp(argv[1], "load-failure")) {
        player.failLoad = true;
        app.onRight();
        CHECK(requests == 0);
        CHECK(app._mode == VideoApp::MODE_BROWSE && app._serverError);
        CHECK(power.locks == 0);
    }
    puts("PASS");
}

