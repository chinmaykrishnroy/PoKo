#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"
#include "TitleMarquee.h"
#include "PokoDrivers.h"
#include "SyncedAVPlayer.h"
#include "AudioManager.h"
#include "PowerManager.h"

extern PowerManager* powerManager;

// ─────────────────────────────────────────────────────────────
//  VideoApp — Single-Thumbnail Video Browser & Synced AV Player
//  Displays exactly ONE video thumbnail on screen at a time with
//  title and duration.
//  Controls:
//    - L:Prv / R:Nxt (Browse videos)
//    - 2R:Play (Start 128×128 synced video stream)
//    - While Playing: 2R:Stop / 2L:Back / Hold: Volume
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern SyncedAVPlayer* syncPlugin;

class VideoApp {
private:
    enum VideoMode {
        MODE_BROWSE,
        MODE_PLAYING
    };

    struct VideoItem {
        char id[36];
        char title[128];
        uint32_t duration_s;
    };

    static constexpr int MAX_CATALOG_ITEMS = 999;
    static constexpr int PAGE_SIZE = 8;

    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool      _active       = false;
    bool      _dirty        = true;
    VideoMode _mode         = MODE_BROWSE;

    VideoItem _videos[PAGE_SIZE];
    int       _videoCount   = 0;
    int       _selectedIdx  = 0;
    int       _pageStart    = -1;
    int       _catalogIndex = 0;
    int       _catalogTotal = 0;
    bool      _loadingList  = false;
    bool      _serverError  = false;
    bool      _serverOffline = false;
    bool      _contentMissing = false;

    uint8_t*  _thumbBuf     = nullptr;
    size_t    _thumbSize    = 0;
    char      _loadedId[36] = {0};

    uint32_t  _lastDrawMs   = 0;
    uint32_t  _playStartMs  = 0;
    bool      _streamStarted = false;
    TitleMarquee _marquee;
    uint16_t _titleWidth = 0;

    static Arduino_Canvas* _activeCanvas;
    static VideoApp*       _instance;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

    String getServerHost();

    int getServerPort();

    bool fetchVideoList(int index = 0, bool loadThumb = true);

    void fetchThumbnail(int idx);

    void failPlayback(int httpCode = 0);

    void requestPlay(int idx, bool retryMissing = true);

    void requestStopInternal();

    void requestStop();

    bool selectVideo(int index);

    void renderToCanvas();

public:
    VideoApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    static bool playbackStoppedStatic();

    static void stopPlaybackStatic();

    void begin();

    void load();

    bool prepareRemoteStream();

    void unload();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void volumeRampDown(int step = 2);

    void volumeRampUp(int step = 2);

    void onBack();

    void onEnter();

    void onPlaybackEnded();

    void update();
};

