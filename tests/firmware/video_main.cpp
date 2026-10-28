uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); return 1; } } while (0)
int main(int argc, char** argv) {
    VideoApp app;
    if (!strcmp(argv[1], "remote-metadata")) {
        RemotePlayback media{"chosen","Chosen video","",120,20,17,40};
        CHECK(app.prepareRemoteMedia(media));
        CHECK(!strcmp(app._videos[0].id,"chosen") && !strcmp(app._videos[0].title,"Chosen video"));
        CHECK(app._videos[0].duration_s==100 && app._catalogIndex==17 && app._catalogTotal==40);
        CHECK(app._selectedIdx==0 && app._videoCount==1 && app._thumbSize==0 && app._loadedId[0]==0);
    } else if (!strcmp(argv[1], "remote-listener-failure")) {
        app.listenerReady=false;
        RemotePlayback media{"chosen","Chosen video"};
        CHECK(!app.prepareRemoteMedia(media));
        CHECK(!strcmp(app._videos[0].id,"video"));
    } else if (!strcmp(argv[1], "server-handoff-no-stop")) {
        app._serverTransition=true;
        app.requestStop();
        CHECK(requests==0 && player.unloads==1 && app.thumbnails==0);
    } else if (!strcmp(argv[1], "next")) {
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
        CHECK(!app._serverOffline && !app._contentMissing);
        CHECK(power.locks == 0 && audio.releases == 1);
        CHECK(!player.loaded);
    } else if (!strcmp(argv[1], "missing-refresh")) {
        httpResult = 404;
        app.onRight();
        CHECK(listRequests == 1 && requests == 2);
        CHECK(app._mode == VideoApp::MODE_PLAYING && !app._serverError && !app._contentMissing);
    } else if (!strcmp(argv[1], "missing-empty")) {
        httpResult = 404;
        listResult = false;
        app.onRight();
        CHECK(listRequests == 2 && requests == 1);
        CHECK(app._mode == VideoApp::MODE_BROWSE && !app._serverError && app._contentMissing);
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
    } else if (!strcmp(argv[1], "fit-query")) {
        prefs.videoFit = true;
        app.onRight();
        CHECK(lastUrl.find("aspect=fit") != std::string::npos);
    } else if (!strcmp(argv[1], "async-rejected")) {
        responseMalformed = true;
        app.requestPlay(0);
        CHECK(app._mode == VideoApp::MODE_BROWSE && app._serverError);
    } else if (!strcmp(argv[1], "async-failed")) {
        app.requestPlay(0);
        testMillis += 1000;
        secondHttpResult = 200;
        responseStatus = "failed";
        CHECK(!app.reconcileOperation());
        CHECK(app._mode == VideoApp::MODE_BROWSE && app._serverError);
    } else if (!strcmp(argv[1], "async-stale-result")) {
        app.requestPlay(0);
        testMillis += 1000;
        secondHttpResult = 200;
        responseRequestId = "older-generation";
        CHECK(!app.reconcileOperation());
    }
    puts("PASS");
}

