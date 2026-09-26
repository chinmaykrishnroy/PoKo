#pragma once
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include "PokoAppState.h"
#include "PokoDrivers.h"
#include "PokoWebUI.h"
#include "PokoTheme.h"
#include "InfoApp.h"
#include "SSyncApp.h"

// ─────────────────────────────────────────────────────────────
//  PokoAPI — Master REST API & Web Dashboard backend
// ─────────────────────────────────────────────────────────────

extern AppState activeApp;
extern void onAppChange(AppState newState);
extern InfoApp* infoAppInstance;
extern SSyncApp* ssyncAppInstance;
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
            json += "\"app_state\":" + String((int)activeApp) + ",";
            json += "\"theme\":\"" + String(isDarkTheme() ? "dark" : "light") + "\",";
            json += "\"master_vol\":" + String(getMasterVolumeLimit()) + ",";
            json += "\"app_vol\":" + String(getCurrentAppVolume()) + ",";
            json += "\"brightness\":" + String(_prefs->getInt("brightness", 80)) + ",";
            json += "\"gallery_timer\":" + String(_prefs->getInt("gallery_timer", 0)) + ",";
            String snapHost = _prefs->getString("snap_host", "192.168.0.20");
            int snapPort = _prefs->getInt("snap_port", 1780);
            json += "\"snap_host\":\"" + snapHost + "\",";
            json += "\"snap_port\":" + String(snapPort);
            json += "}";
            _server->send(200, "application/json", json);
        });

        // System settings (brightness, app volume, master volume limiter)
        _server->on("/api/sys", HTTP_GET, [this]() {
            if (_server->hasArg("brightness")) {
                int b = constrain(_server->arg("brightness").toInt(), 1, 100);
                _prefs->putInt("brightness", b);
                setBacklightPercent(b);
            }
            if (_server->hasArg("volume")) {
                int v = constrain(_server->arg("volume").toInt(), 0, 100);
                _prefs->putInt("volume", v);
                setScaledVolume(v);
            }
            if (_server->hasArg("master_vol")) {
                int mv = constrain(_server->arg("master_vol").toInt(), 1, 100);
                _prefs->putInt("master_vol", mv);
                setMasterVolumeLimit(mv);
            }
            int curB = _prefs->getInt("brightness", 80);
            int curV = getCurrentAppVolume();
            int curM = getMasterVolumeLimit();
            String resp = "{\"ok\":true,\"brightness\":" + String(curB) +
                          ",\"volume\":" + String(curV) +
                          ",\"master_vol\":" + String(curM) + "}";
            _server->send(200, "application/json", resp);
        });

        // Theme endpoint (Dark / Light)
        _server->on("/api/theme", HTTP_GET, [this]() {
            if (_server->hasArg("mode")) {
                String m = _server->arg("mode");
                if (m == "dark") {
                    setPokoTheme(true);
                    _prefs->putString("ui_theme", "dark");
                } else if (m == "light") {
                    setPokoTheme(false);
                    _prefs->putString("ui_theme", "light");
                }
            }
            String mode = isDarkTheme() ? "dark" : "light";
            _server->send(200, "application/json", "{\"ok\":true,\"theme\":\"" + mode + "\"}");
        });

        // Snapclient / SSync config & status
        _server->on("/api/snap", HTTP_GET, [this]() {
            if (_server->hasArg("host")) {
                String h = _server->arg("host");
                uint16_t p = _server->hasArg("port") ? _server->arg("port").toInt() : 1780;
                _prefs->putString("snap_host", h);
                _prefs->putInt("snap_port", p);
                // Binary streaming port for Snapcast is 1704 if 1780 (control port) is entered
                uint16_t streamPort = (p == 1780) ? 1704 : p;
                if (ssyncAppInstance && ssyncAppInstance->getPlayer()) {
                    ssyncAppInstance->getPlayer()->setServer(h, streamPort);
                }
            }
            if (_server->hasArg("vol")) {
                int v = constrain(_server->arg("vol").toInt(), 0, 100);
                if (ssyncAppInstance && ssyncAppInstance->getPlayer()) {
                    ssyncAppInstance->getPlayer()->setVolumePercent(v);
                }
            }
            if (_server->hasArg("mute")) {
                if (ssyncAppInstance && ssyncAppInstance->getPlayer()) {
                    ssyncAppInstance->getPlayer()->toggleMute();
                }
            }
            String j = (ssyncAppInstance) ? ssyncAppInstance->apiJson() : "{}";
            _server->send(200, "application/json", j);
        });

        // Gallery slideshow settings
        _server->on("/api/gallery", HTTP_GET, [this]() {
            if (_server->hasArg("timer")) {
                int t = constrain(_server->arg("timer").toInt(), 0, 3600);
                _prefs->putInt("gallery_timer", t);
            }
            int t = _prefs->getInt("gallery_timer", 0);
            _server->send(200, "application/json", "{\"ok\":true,\"timer\":" + String(t) + "}");
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
