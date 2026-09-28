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

    static constexpr uint8_t ROW_H         = 14;
    static constexpr uint8_t TOP_Y         = 14;
    static constexpr uint8_t ROWS_VISIBLE  = 7;
    static constexpr uint8_t FOOTER_Y      = 114;

    struct Row { char label[14]; char value[24]; uint16_t valColor; };
    Row     _rows[32];
    uint8_t _rowCount = 0;

    void buildRows() {
        _rowCount = 0;
        auto add = [&](const char* lbl, String val, uint16_t col = POKO_CLR_TEXT) {
            if (_rowCount >= 32) return;
            strncpy(_rows[_rowCount].label, lbl, 13);
            _rows[_rowCount].label[13] = '\0';
            strncpy(_rows[_rowCount].value, val.c_str(), 23);
            _rows[_rowCount].value[23] = '\0';
            _rows[_rowCount].valColor = col;
            _rowCount++;
        };

        char buf[26];

        // 1. Battery & Power
        if (batteryManager.isPresent()) {
            int pct = batteryManager.getPercentage();
            float v = batteryManager.getVoltage();
            bool chg = batteryManager.isCharging();
            snprintf(buf, sizeof(buf), "%d%% (%.2fV)", pct, v);
            uint16_t batCol = chg ? POKO_CLR_GREEN : (pct <= 15 ? POKO_CLR_ERR : (pct <= 30 ? POKO_CLR_WARN : POKO_CLR_GREEN));
            add("Battery", buf, batCol);

            const char* stateStr = chg ? "Charging" : (batteryManager.isFull() ? "Full (100%)" : (batteryManager.isCritical() ? "Critical (<3.25V)" : "Discharging"));
            add("Bat State", stateStr, chg ? POKO_CLR_GREEN : (batteryManager.isCritical() ? POKO_CLR_ERR : POKO_CLR_TEXT));
        } else {
            add("Battery", "None (USB Only)", POKO_CLR_ACCENT);
            add("Power Src", "5V USB VBUS", POKO_CLR_GREEN);
        }

        if (powerManager) {
            add("USB Perf", powerManager->isUsbPerfMax() ? "Max Perf (240MHz)" : "Managed", POKO_CLR_TEXT);
        }

        // 2. Firmware Version & Build
        add("Version", "v1.2.0 (PoKo)", POKO_CLR_ACCENT);
        add("Build", __DATE__, POKO_CLR_DIM);

        // 3. Reset Reason
        esp_reset_reason_t rst = esp_reset_reason();
        const char* rstStr = "Unknown";
        if (rst == ESP_RST_POWERON) rstStr = "Power-On";
        else if (rst == ESP_RST_SW) rstStr = "Software Restart";
        else if (rst == ESP_RST_DEEPSLEEP) rstStr = "Deep Sleep Wake";
        else if (rst == ESP_RST_PANIC) rstStr = "Exception/Crash";
        else if (rst == ESP_RST_INT_WDT || rst == ESP_RST_TASK_WDT || rst == ESP_RST_WDT) rstStr = "Watchdog";
        else if (rst == ESP_RST_BROWNOUT) rstStr = "Brownout";
        add("Reset Rsn", rstStr, (rst == ESP_RST_PANIC || rst == ESP_RST_WDT) ? POKO_CLR_ERR : POKO_CLR_DIM);

        // 4. Memory (Heap & PSRAM)
        uint32_t freeH   = ESP.getFreeHeap();
        uint32_t minH    = ESP.getMinFreeHeap();
        uint32_t freePSR = ESP.getFreePsram();
        snprintf(buf, sizeof(buf), "%u KB free", (unsigned)(freeH / 1024));
        add("Heap", buf, freeH < 50000 ? POKO_CLR_WARN : POKO_CLR_GREEN);

        snprintf(buf, sizeof(buf), "%u KB min", (unsigned)(minH / 1024));
        add("Min Heap", buf, minH < 35000 ? POKO_CLR_WARN : POKO_CLR_TEXT);

        snprintf(buf, sizeof(buf), "%.2f / 8 MB", freePSR / (1024.0f * 1024.0f));
        add("PSRAM", buf, POKO_CLR_TEXT);

        // 5. Hardware Specifications
        snprintf(buf, sizeof(buf), "%u MHz (Dual)", (unsigned)getCpuFrequencyMhz());
        add("CPU", buf, POKO_CLR_TEXT);

        esp_chip_info_t ci;
        esp_chip_info(&ci);
        snprintf(buf, sizeof(buf), "ESP32-S3 r%d", ci.revision);
        add("SoC", buf, POKO_CLR_DIM);

        snprintf(buf, sizeof(buf), "%u MB (QIO)", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
        add("Flash", buf, POKO_CLR_TEXT);

        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getSketchSize() / 1024));
        add("App Size", buf, POKO_CLR_DIM);

        // 6. Display & Power Subsystem
        add("Display", "128x128 GC9107", POKO_CLR_DIM);

        int curBr = powerManager ? powerManager->getUserBrightnessPercent() : 80;
        snprintf(buf, sizeof(buf), "%d%% (PWM)", curBr);
        add("Backlight", buf, POKO_CLR_TEXT);

        const char* dispPwr = "Active";
        if (powerManager) {
            DisplayPowerState dps = powerManager->getDisplayState();
            if (dps == DISPLAY_POWER_DIMMED) dispPwr = "Dimmed";
            else if (dps == DISPLAY_POWER_SLEEP) dispPwr = "Sleep";
            else if (dps == DISPLAY_POWER_OFF) dispPwr = "Off";
        }
        add("Pwr State", dispPwr, POKO_CLR_ACCENT);

        uint32_t locks = powerManager ? powerManager->getLocks() : 0;
        if (locks == 0) {
            snprintf(buf, sizeof(buf), "None");
        } else {
            String lStr = "";
            if (locks & POWER_LOCK_AUDIO) lStr += "Audio ";
            if (locks & POWER_LOCK_REALTIME_NET) lStr += "Net ";
            if (locks & POWER_LOCK_DISPLAY) lStr += "Disp ";
            if (locks & POWER_LOCK_OTA) lStr += "OTA ";
            snprintf(buf, sizeof(buf), "%s", lStr.c_str());
        }
        add("Pwr Locks", buf, locks ? POKO_CLR_WARN : POKO_CLR_DIM);

        // 7. Audio Subsystem
        add("Codec", "ES8311 (I2S)", POKO_CLR_DIM);
        const char* aSrc = "Idle / None";
        if (audioManager) {
            AudioSource as = audioManager->activeSource();
            if (as == AUDIO_SSYNC) aSrc = "SSync (Snap)";
            else if (as == AUDIO_MUSIC) aSrc = "Music Player";
            else if (as == AUDIO_VIDEO) aSrc = "Video AV";
        }
        add("Audio Src", aSrc, (audioManager && audioManager->activeSource() != AUDIO_NONE) ? POKO_CLR_GREEN : POKO_CLR_DIM);

        add("Amp PA", isSpeakerAmpEnabled() ? "Active (On)" : "Standby (Off)", isSpeakerAmpEnabled() ? POKO_CLR_GREEN : POKO_CLR_DIM);

        snprintf(buf, sizeof(buf), "%d%% (+%ddB)", getCurrentAppVolume(), getAmpBoostDb());
        add("Volume", buf, POKO_CLR_TEXT);

        // 8. Network (Wi-Fi & Storage)
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "No STA (AP)";
        add("IP", ip, POKO_CLR_ACCENT);

        add("SSID", WiFi.SSID().length() ? WiFi.SSID() : (WiFi.getMode() & WIFI_AP ? "POKO_SETUP" : "Disconnected"), POKO_CLR_TEXT);

        if (WiFi.status() == WL_CONNECTED) {
            snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
            add("RSSI", buf, POKO_CLR_DIM);
        }

        uint8_t mac[6]; WiFi.macAddress(mac);
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        add("MAC", buf, POKO_CLR_DIM);

        snprintf(buf, sizeof(buf), "%u / %u KB", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
        add("LittleFS", buf, POKO_CLR_DIM);

        // 9. Time & Uptime
        uint32_t sec = millis() / 1000;
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)(sec / 3600),
                 (unsigned long)((sec % 3600) / 60),
                 (unsigned long)(sec % 60));
        add("Uptime", buf, POKO_CLR_TEXT);

        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2020 - 1900)) {
            char timeBuf[20];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
            add("Time", timeBuf, POKO_CLR_ACCENT);
        }
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 13, theme.headerBg);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.headerText, theme.headerBg);
        _canvas->setCursor(3, 10);
        _canvas->print("System Info");

        if (audioManager) {
            audioManager->drawStatusDot(_canvas, 70, 6, 2);
        }

        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d/%d", _scroll + 1, _rowCount);
        _canvas->setTextColor(theme.muted, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 10);
        _canvas->print(countBuf);

        // Rows (y=14..113)
        _canvas->fillRect(0, TOP_Y, 128, FOOTER_Y - TOP_Y, theme.bg);
        _canvas->setFont(u8g2_font_profont10_mf);

        for (uint8_t i = 0; i < ROWS_VISIBLE; i++) {
            uint8_t ri = _scroll + i;
            if (ri >= _rowCount) break;
            int16_t rowY = TOP_Y + i * ROW_H;
            int16_t textY = rowY + ROW_H - 3;

            if (i % 2 == 0) {
                _canvas->fillRect(0, rowY, 124, ROW_H, theme.surface);
            }

            _canvas->setTextColor(theme.muted, i % 2 == 0 ? theme.surface : theme.bg);
            _canvas->setCursor(3, textY);
            _canvas->print(_rows[ri].label);

            _canvas->setTextColor(_rows[ri].valColor, i % 2 == 0 ? theme.surface : theme.bg);
            _canvas->getTextBounds(_rows[ri].value, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(123 - w, textY);
            _canvas->print(_rows[ri].value);
        }

        // Scroll indicator bar
        if (_rowCount > ROWS_VISIBLE) {
            uint8_t barH = (ROWS_VISIBLE * (FOOTER_Y - TOP_Y)) / _rowCount;
            uint8_t barY = TOP_Y + (_scroll * (FOOTER_Y - TOP_Y)) / _rowCount;
            _canvas->drawFastVLine(126, TOP_Y, FOOTER_Y - TOP_Y, theme.line);
            _canvas->drawFastVLine(126, barY, barH, theme.accent);
        }

        // Footer (y=114..127) - Filled with navigation guide
        _canvas->fillRect(0, FOOTER_Y, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, FOOTER_Y, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        const char* footerHint = "L:Prev  R:Next  2R:Exit";
        _canvas->getTextBounds(footerHint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(footerHint);

        // Single atomic flush -> 100% flicker-free!
        _canvas->flush();
    }

public:
    InfoApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active = true;
        _scroll = 0;
        _dirty  = true;
        begin();
        buildRows();
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        if (_scroll > 0) {
            _scroll--;
            _dirty = true;
        }
    }

    void onRight() {
        if (_scroll + ROWS_VISIBLE < _rowCount) {
            _scroll++;
            _dirty = true;
        }
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        onBack();
    }

    void onLongRight() {
        buildRows();
        _scroll = 0;
        _dirty  = true;
    }

    void onLongLeft() {}

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        if (audioBlinking && (now - _lastBlinkMs >= 100)) {
            _lastBlinkMs = now;
            _dirty = true;
        }
        if (now - _lastDrawMs >= 1000) {
            buildRows();
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        _lastDrawMs = now;
        renderToCanvas();
    }

    String apiJson() {
        buildRows();
        String j = "{";
        j += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "n/a") + "\",";
        j += "\"ssid\":\"" + WiFi.SSID() + "\",";
        j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        j += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
        j += "\"psram_free\":" + String(ESP.getFreePsram()) + ",";
        j += "\"uptime_ms\":" + String(millis()) + ",";
        j += "\"cpu_mhz\":" + String(getCpuFrequencyMhz());
        j += "}";
        return j;
    }
};
