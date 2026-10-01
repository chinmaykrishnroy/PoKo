#pragma once
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <esp_heap_caps.h>
#include "PokoAppState.h"
#include "PokoDrivers.h"
#include "PokoWebUI.h"
#include "PokoTheme.h"
#include "InfoApp.h"
#include "SSyncApp.h"
#include "MusicApp.h"
#include "VideoApp.h"
#include "GalleryApp.h"
#include "BatteryManager.h"
#include "PowerManager.h"

// ─────────────────────────────────────────────────────────────
//  PokoAPI — Master REST API & Web Dashboard backend
// ─────────────────────────────────────────────────────────────

extern AppState activeApp;
extern void onAppChange(AppState newState);
extern void refreshActiveAppTheme();
extern InfoApp* infoAppInstance;
extern SSyncApp* ssyncAppInstance;
extern MusicApp* musicAppInstance;
extern VideoApp* videoAppInstance;
extern GalleryApp* galleryAppInstance;
extern bool handleDriverReset();
extern void onBtnLeft();
extern void onBtnRight();
extern void onBtnLeftDouble();
extern void onBtnRightDouble();
extern BatteryManager batteryManager;
extern PowerManager* powerManager;
extern AudioManager* audioManager;

class PokoAPI {
private:
    WebServer*   _server;
    Preferences* _prefs;
    bool         _uploadSuccess = false;
    String       _uploadErrMsg  = "";

    static bool validateJpegBlock(int16_t, int16_t, uint16_t, uint16_t, uint16_t*);

    static bool isDecodableJpeg(const uint8_t* bytes, size_t size);

public:
    PokoAPI(WebServer* srv, Preferences* prf);

    void begin();
};

