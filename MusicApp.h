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
#include "PokoDrivers.h"
#include "PokoTheme.h"
#include "TCPAudio.h"
#include "AudioManager.h"

// ─────────────────────────────────────────────────────────────
//  MusicApp — Single-Song Audio Browser & MP3 TCP Stream Player
//  Displays exactly ONE song at a time with album art thumbnail,
//  title, artist, and duration.
//  Controls:
//    - L:Prv / R:Nxt (Browse songs)
//    - 2R:Play (Start MP3 TCP streaming playback over TCPAudio)
//    - While Playing: L:V- / R:V+ / 2R:Stop / 2L:Back
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern TCPAudio* audioPlugin;

class MusicApp {
private:
    enum MusicMode {
        MODE_BROWSE,
        MODE_PLAYING
    };

    struct SongItem {
        char id[36];
        char title[44];
        char artist[28];
        uint32_t duration_s;
    };

    static constexpr int MAX_CATALOG_ITEMS = 999;
    static constexpr int PAGE_SIZE = 8;

    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool      _active       = false;
    bool      _dirty        = true;
    bool      _paused       = false;
    MusicMode _mode         = MODE_BROWSE;

    SongItem  _songs[PAGE_SIZE];
    int       _songCount    = 0;
    int       _selectedIdx  = 0;
    int       _pageStart    = -1;
    int       _catalogIndex = 0;
    int       _catalogTotal = 0;
    bool      _loadingList  = false;
    bool      _serverError  = false;
    bool      _serverOffline = false;
    bool      _contentMissing = false;

    uint8_t*  _artBuf       = nullptr;
    size_t    _artSize      = 0;
    char      _loadedId[36] = {0};
    uint16_t* _artBitmap    = nullptr;
    bool      _artBitmapValid = false;
    char      _artRequestedId[36] = {0};
    uint8_t   _artFailures = 0;
    uint32_t  _lastArtAttemptMs = 0;

    uint32_t  _trackPos     = 0;
    uint32_t  _playStartMs  = 0;
    uint32_t  _streamRequestMs = 0;
    bool      _streamStarted = false;
    uint32_t  _lastSecondMs = 0;
    uint32_t  _lastDrawMs   = 0;
    int       _scrollOffset = 0;
    uint32_t  _lastScrollMs = 0;

    static uint16_t* _decodeTarget;
    static int16_t   _decodeTargetW;
    static int16_t   _decodeTargetH;
    static bool      _needsColorExtract;

    static bool tftDecodeBitmap(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

    void decodeArtworkToBitmap();

    String getServerHost();

    int getServerPort();

    bool fetchSongList(int index = 0, bool loadArt = true);

    void fetchArtwork(int idx);

    bool requestPlay(int idx, uint32_t startSec = 0, bool retryMissing = true);

    void requestStop();

    bool selectSong(int index);

    void renderToCanvas();

public:
    MusicApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    ~MusicApp();

    void begin();

    void pausePlayback();

    void resumePlayback();

    void stopPlaybackInternal();

    void stopPlayback();

    void togglePlayPause();

    bool prepareRemoteStream();

    void load();

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

    bool isPlaying() const;

    const char* getCurrentTitle() const;

    bool hasServerError() const;
};

