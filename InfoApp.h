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
//
//  Shows: IP, WiFi SSID, RSSI, Free Heap, PSRAM, Uptime, CPU,
//         Flash Size, Chip Revision, Battery/Voltage (if any).
//
//  LEFT (Boot)  = scroll up
//  RIGHT (Key)  = scroll down
//  LEFT double  = back / exit to launcher
//  LONG RIGHT   = refresh stats
// ─────────────────────────────────────────────────────────────

extern String getNetworkStatusMsg();

class InfoApp {
private:
    Arduino_GFX*  _gfx;
    AppSwitchFn   _exit;

    bool          _active     = false;
    bool          _dirty      = true;
    uint8_t       _scroll     = 0;
    uint32_t      _lastDrawMs = 0;

    static constexpr uint8_t ROW_H         = 13;
    static constexpr uint8_t TOP_Y         = 14;
    static constexpr uint8_t ROWS_VISIBLE  = 8;

    struct Row { char label[14]; char value[18]; uint16_t valColor; };
    Row     _rows[14];
    uint8_t _rowCount = 0;

    void buildRows() {
        _rowCount = 0;
        auto add = [&](const char* lbl, String val, uint16_t col = POKO_CLR_TEXT) {
            if (_rowCount >= 14) return;
            strncpy(_rows[_rowCount].label, lbl,  13);
            _rows[_rowCount].label[13] = '\0';
            strncpy(_rows[_rowCount].value, val.c_str(), 17);
            _rows[_rowCount].value[17] = '\0';
            _rows[_rowCount].valColor = col;
            _rowCount++;
        };

        // Network
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "No STA (AP?)";
        add("IP",       ip,           POKO_CLR_ACCENT);
        add("SSID",     WiFi.SSID().length() ? WiFi.SSID() : "Disconnected", POKO_CLR_TEXT);
        add("RSSI",     WiFi.status() == WL_CONNECTED
                            ? (String(WiFi.RSSI()) + " dBm") : "---", POKO_CLR_DIM);

        // Memory
        uint32_t freeH  = ESP.getFreeHeap();
        uint32_t freePSR = ESP.getFreePsram();
        char buf[20];
        snprintf(buf, sizeof(buf), "%u KB", freeH / 1024);
        add("Heap",     buf,  freeH < 50000 ? POKO_CLR_WARN : POKO_CLR_GREEN);
        snprintf(buf, sizeof(buf), "%u KB", freePSR / 1024);
        add("PSRAM",    buf,  POKO_CLR_TEXT);

        // Local Time (IST)
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > (2020 - 1900)) {
            char timeBuf[18];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
            add("Time", timeBuf, POKO_CLR_ACCENT);
        }

        // Uptime
        uint32_t sec = millis() / 1000;
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)(sec / 3600),
                 (unsigned long)((sec % 3600) / 60),
                 (unsigned long)(sec % 60));
        add("Uptime",   buf,  POKO_CLR_TEXT);

        // Hardware
        snprintf(buf, sizeof(buf), "%u MHz", (unsigned)getCpuFrequencyMhz());
        add("CPU",      buf,  POKO_CLR_TEXT);

        snprintf(buf, sizeof(buf), "%u MB", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
        add("Flash",    buf,  POKO_CLR_TEXT);

        esp_chip_info_t ci;
        esp_chip_info(&ci);
        snprintf(buf, sizeof(buf), "v%d (%d cores)", ci.revision, ci.cores);
        add("ESP32-S3", buf,  POKO_CLR_DIM);

        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getSketchSize() / 1024));
        add("App Size", buf,  POKO_CLR_DIM);

        uint8_t mac[6]; WiFi.macAddress(mac);
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3]);
        add("MAC",      buf,  POKO_CLR_DIM);
    }

    void drawHeader() {
        _gfx->fillRect(0, 0, 128, 13, 0x0841);
        _gfx->setFont(u8g2_font_profont10_mf);
        _gfx->setTextColor(POKO_CLR_ACCENT, 0x0841);
        _gfx->setCursor(3, 10);
        _gfx->print("System Info");

        char buf[8];
        snprintf(buf, sizeof(buf), "%d/%d", _scroll + 1, _rowCount);
        _gfx->setTextColor(POKO_CLR_DIM, 0x0841);
        int16_t x1, y1; uint16_t w, h;
        _gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(125 - w, 10);
        _gfx->print(buf);
    }

    void drawRows() {
        _gfx->fillRect(0, TOP_Y, 128, 128 - TOP_Y, POKO_CLR_BG);
        _gfx->setFont(u8g2_font_profont10_mf);

        for (uint8_t i = 0; i < ROWS_VISIBLE; i++) {
            uint8_t ri = _scroll + i;
            if (ri >= _rowCount) break;
            int16_t y = TOP_Y + i * ROW_H + ROW_H - 2;

            if (i % 2 == 0) _gfx->fillRect(0, TOP_Y + i * ROW_H, 125, ROW_H, 0x0821);

            _gfx->setTextColor(POKO_CLR_DIM, i % 2 == 0 ? 0x0821 : POKO_CLR_BG);
            _gfx->setCursor(3, y);
            _gfx->print(_rows[ri].label);

            _gfx->setTextColor(_rows[ri].valColor, i % 2 == 0 ? 0x0821 : POKO_CLR_BG);
            int16_t x1, y1; uint16_t w, h;
            _gfx->getTextBounds(_rows[ri].value, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(124 - w, y);
            _gfx->print(_rows[ri].value);
        }

        // Scroll bar
        if (_rowCount > ROWS_VISIBLE) {
            uint8_t barH = (ROWS_VISIBLE * (128 - TOP_Y)) / _rowCount;
            uint8_t barY = TOP_Y + (_scroll * (128 - TOP_Y)) / _rowCount;
            _gfx->drawFastVLine(127, TOP_Y, 128 - TOP_Y, POKO_CLR_DIM);
            _gfx->drawFastVLine(127, barY, barH, POKO_CLR_ACCENT);
        }
    }

public:
    InfoApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {}

    void load() {
        _active = true;
        _scroll = 0;
        _dirty  = true;
        buildRows();
    }

    void unload() {
        _active = false;
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        if (_scroll > 0) { _scroll--; _dirty = true; }
    }

    void onRight() {
        if (_scroll + ROWS_VISIBLE < _rowCount) { _scroll++; _dirty = true; }
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {}

    void onLongRight() {
        buildRows();
        _scroll = 0;
        _dirty  = true;
    }

    void onLongLeft() {}

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        if (now - _lastDrawMs >= 1000) {
            buildRows();
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        _lastDrawMs = now;
        drawHeader();
        drawRows();
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
