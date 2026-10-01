#pragma once
#include <WebServer.h>
#include <Update.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include "PokoAppState.h"
#include "PixelEngine.h"
#include "SnapPlayer.h"
#include "AudioManager.h"
#include "PowerManager.h"

extern SnapPlayer*   snapService;
extern PowerManager* powerManager;

// ─────────────────────────────────────────────────────────────
//  PokoOTA — Web-based OTA firmware updates and progress screen
// ─────────────────────────────────────────────────────────────

#include "PokoOTAWeb.h"

class PokoOTA {
private:
    static size_t _otaTotal;
    static size_t _otaWritten;
    static int    _otaLastPct;

    static void drawProgress(Arduino_GFX* gfx, int pct);

public:
    static void begin(WebServer* server, AppSwitchFn switchCb, Arduino_GFX* gfx);
};

