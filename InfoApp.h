#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoUI.h"

// ─────────────────────────────────────────────────────────────
//  InfoApp — System information screen (128×128)
//  Uses Arduino_Canvas for zero-flicker double-buffered rendering.
//
//  Shows: IP, SSID, RSSI, Free Heap, PSRAM, Time, Uptime, CPU,
//         Flash Size, Chip Revision, App Size, MAC address.
//
//  BOOT (Left)  = scroll up
//  KEY (Right)  = scroll down
//  BOOT Double  = back / exit to launcher
//  KEY Long     = refresh stats
// ─────────────────────────────────────────────────────────────

#include "BatteryManager.h"
#include "PowerManager.h"
#include "AudioManager.h"
#include "PokoDrivers.h"
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_chip_info.h>

extern BatteryManager batteryManager;
extern PowerManager* powerManager;
extern AudioManager* audioManager;
extern String getNetworkStatusMsg();
extern String apPassword;

class InfoApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool          _active     = false;
    bool          _dirty      = true;
    uint8_t       _scroll     = 0;
    uint32_t      _lastDrawMs = 0;
    uint32_t      _lastBlinkMs = 0;
    uint32_t      _lastRowBuildMs = 0;
    bool          _lastChargingState = false;

    static constexpr uint8_t ROW_H         = 14;
    static constexpr uint8_t TOP_Y         = 14;
    static constexpr uint8_t ROWS_VISIBLE  = 7;
    static constexpr uint8_t FOOTER_Y      = 114;

    struct Row { char label[14]; char value[24]; uint16_t valColor; };
    Row     _rows[32];
    uint8_t _rowCount = 0;

    void buildRows();

public:
    void renderToCanvas();

public:
    InfoApp(Arduino_GFX* gfx, AppSwitchFn exitFn);

    void begin();

    void load();

    void unload();

    bool isLoaded() const;

    void refreshTheme();

    void onLeft();

    void onRight();

    void onBack();

    void onEnter();

    void onLongRight();

    void onLongLeft();

    void update();

    String apiJson();
};

