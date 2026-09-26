#pragma once
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include "PokoAppState.h"
#include "PokoDrivers.h"
#include "PokoWebUI.h"
#include "InfoApp.h"

// ─────────────────────────────────────────────────────────────
//  PokoAPI — Master REST API & Web Dashboard backend
// ─────────────────────────────────────────────────────────────

extern AppState activeApp;
extern void onAppChange(AppState newState);
extern InfoApp* infoAppInstance;
extern void handleDriverReset();

class PokoAPI {
private:
    WebServer*   _server;
    Preferences* _prefs;

public:
    PokoAPI(WebServer* srv, Preferences* prf)
        : _server(srv), _prefs(prf) {}

    void begin() {
        // Web Dashboard root
        _server->on("/", HTTP_GET, [this]() {
            _server->sendHeader("Cache-Control", "no-store");
            _server->send_P(200, "text/html; charset=utf-8", poko_web_html);
        });

        // Health / Status endpoint
        _server->on("/api/health", HTTP_GET, [this]() {
            String json = "{";
            json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "n/a") + "\",";
            json += "\"ssid\":\"" + WiFi.SSID() + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
            json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
            json += "\"psram_free\":" + String(ESP.getFreePsram()) + ",";
            json += "\"uptime_ms\":" + String(millis()) + ",";
            json += "\"cpu_mhz\":" + String(getCpuFrequencyMhz()) + ",";
            json += "\"app_state\":" + String((int)activeApp);
            json += "}";
            _server->send(200, "application/json", json);
        });

        // System settings (brightness, volume)
        _server->on("/api/sys", HTTP_GET, [this]() {
            if (_server->hasArg("brightness")) {
                int b = constrain(_server->arg("brightness").toInt(), 1, 100);
                _prefs->putInt("brightness", b);
                setBacklightPercent(b);
            }
            if (_server->hasArg("volume")) {
                int v = constrain(_server->arg("volume").toInt(), 0, 100);
                _prefs->putInt("volume", v);
                es8311SetVolume(v);
            }
            int curB = _prefs->getInt("brightness", 80);
            int curV = _prefs->getInt("volume", 75);
            String resp = "{\"ok\":true,\"brightness\":" + String(curB) + ",\"volume\":" + String(curV) + "}";
            _server->send(200, "application/json", resp);
        });

        // App switch
        _server->on("/api/app", HTTP_GET, [this]() {
            if (_server->hasArg("state")) {
                int s = _server->arg("state").toInt();
                if (s >= 0 && s < STATE_COUNT) {
                    onAppChange((AppState)s);
                }
            }
            _server->send(200, "application/json", "{\"ok\":true,\"app\":" + String((int)activeApp) + "}");
        });

        // WS2812B LED Control
        _server->on("/api/led", HTTP_GET, [this]() {
            if (_server->hasArg("r") && _server->hasArg("g") && _server->hasArg("b")) {
                uint8_t r = constrain(_server->arg("r").toInt(), 0, 255);
                uint8_t g = constrain(_server->arg("g").toInt(), 0, 255);
                uint8_t b = constrain(_server->arg("b").toInt(), 0, 255);
                setAllLEDs(CRGB(r, g, b));
            } else if (_server->hasArg("off")) {
                turnOffLEDs();
            }
            _server->send(200, "application/json", "{\"ok\":true}");
        });

        // WiFi credentials save & reconnect (reboot)
        _server->on("/api/wifi", HTTP_POST, [this]() {
            String s = "";
            String p = "12345678";
            if (_server->hasArg("ssid")) s = _server->arg("ssid");
            if (_server->hasArg("pass")) p = _server->arg("pass");

            if (s.length() > 0) {
                Serial.printf("[api] saving WiFi SSID: %s\n", s.c_str());
                _prefs->putString("wifi_ssid", s);
                _prefs->putString("wifi_pass", p);
                _server->send(200, "application/json", "{\"ok\":true,\"status\":\"rebooting\"}");
                delay(600);
                ESP.restart();
                return;
            }
            _server->send(400, "application/json", "{\"ok\":false,\"error\":\"missing ssid\"}");
        });

        // Driver reset
        _server->on("/api/reset", HTTP_GET, [this]() {
            handleDriverReset();
            _server->send(200, "application/json", "{\"ok\":true,\"msg\":\"Drivers reset\"}");
        });

        // Reboot
        _server->on("/api/reboot", HTTP_GET, [this]() {
            _server->send(200, "application/json", "{\"ok\":true,\"msg\":\"Rebooting\"}");
            delay(500);
            ESP.restart();
        });
    }
};
