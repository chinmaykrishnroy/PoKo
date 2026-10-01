#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <esp_task_wdt.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"
#include "TitleMarquee.h"
#include "PowerManager.h"
#include "AudioManager.h"

// ─────────────────────────────────────────────────────────────
//  GalleryApp — Photo viewer (128×128)
//  Shows LittleFS uploaded images FIRST, then Server library images.
//  In windowed mode: 64×64 thumbnail, title, counter & hints.
//  Auto-fullscreen after 2s, or after one complete long-title marquee pass.
//  Controls:
//    Single Left: Previous photo
//    Single Right: Next photo
//    Double Left: Exit fullscreen / Exit to launcher
//    Double Right: Toggle fullscreen
//  Slideshow auto-advance supported via Preferences.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern PowerManager* powerManager;

class GalleryApp {
public:
    enum PhotoSource : uint8_t { PHOTO_LITTLEFS, PHOTO_SERVER };

    struct GalleryItem {
        PhotoSource source;
        char idOrPath[64];
        char title[128];
        size_t fileSize;
    };

    static constexpr int MAX_GALLERY_PHOTOS = 999;

private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active          = false;
    bool     _dirty           = true;
    bool     _fullscreen      = false;
    int      _photoIdx        = 0;
    int      _photoCount      = 0;
    int      _localCount      = 0;
    uint32_t _lastActivityMs  = 0;
    uint32_t _lastSlideMs     = 0;
    uint32_t _lastBlinkMs     = 0;
    TitleMarquee _marquee;
    uint16_t _titleWidth = 0;

    GalleryItem _photos[1] = {};

    uint8_t* _imgBuf      = nullptr;
    size_t   _imgSize     = 0;
    int      _loadedIdx   = -1;
    bool     _loadFailed  = false;
    bool     _loading     = false;
    bool     _serverOffline = false;
    bool     _serverError = false;
    bool     _contentMissing = false;

    static Arduino_Canvas* _activeCanvas;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

    void scanPhotos();

    bool selectCurrentPhoto();

    void loadCurrentPhoto(bool retryMissing = true);

    void renderToCanvas();

public:
    GalleryApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    void begin();

    void load();

    void unload();

    void reloadList();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void onBack();

    void onEnter();

    void update();
};

