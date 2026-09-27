#pragma once
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "PokoAppState.h"
#include "PokoDrivers.h"
#include "PokoWebUI.h"
#include "PokoTheme.h"
#include "InfoApp.h"
#include "SSyncApp.h"
#include "MusicApp.h"
#include "VideoApp.h"
#include "GalleryApp.h"

// ─────────────────────────────────────────────────────────────
//  PokoAPI — Master REST API & Web Dashboard backend
// ─────────────────────────────────────────────────────────────

extern AppState activeApp;
extern void onAppChange(AppState newState);
extern InfoApp* infoAppInstance;
extern SSyncApp* ssyncAppInstance;
extern MusicApp* musicAppInstance;
extern VideoApp* videoAppInstance;
extern GalleryApp* galleryAppInstance;
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
            json += "\"amp_boost\":" + String(getAmpBoostDb()) + ",";
            json += "\"gallery_timer\":" + String(_prefs->getInt("gallery_timer", 0)) + ",";
            String snapHost = _prefs->getString("snap_host", "192.168.0.20");
            int snapPort = _prefs->getInt("snap_port", 1704);
            json += "\"snap_host\":\"" + snapHost + "\",";
            json += "\"snap_port\":" + String(snapPort) + ",";
            String srvHost = _prefs->getString("server_host", "192.168.0.15");
            int srvPort = _prefs->getInt("server_port", 8765);
            json += "\"server_host\":\"" + srvHost + "\",";
            json += "\"server_port\":" + String(srvPort) + ",";
            json += "\"server_addr\":\"" + srvHost + ":" + String(srvPort) + "\",";
            json += "\"pixel_mode\":" + String((int)pixelEngine.getMode()) + ",";
            json += "\"pixel_r\":" + String(pixelEngine.getR()) + ",";
            json += "\"pixel_g\":" + String(pixelEngine.getG()) + ",";
            json += "\"pixel_b\":" + String(pixelEngine.getB()) + ",";
            json += "\"pixel_bright\":" + String(pixelEngine.getBrightness()) + ",";
            json += "\"pixel_target\":" + String(pixelEngine.getTargetPixel()) + ",";
            json += "\"target_mask\":" + String((int)pixelEngine.getTargetMask()) + ",";
            json += "\"target_label\":\"" + String(pixelEngine.getTargetMaskLabel()) + "\",";
            json += "\"music_light\":" + String(pixelEngine.getMusicLightOn() ? "true" : "false") + ",";
            json += "\"music_effect\":" + String((int)pixelEngine.getMusicEffect()) + ",";
            json += "\"ssync_light\":" + String(pixelEngine.getSSyncLightOn() ? "true" : "false") + ",";
            json += "\"ssync_effect\":" + String((int)pixelEngine.getSSyncEffect()) + ",";
            json += "\"freq_resp\":" + String((int)pixelEngine.getFreqResponse());
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
            if (_server->hasArg("amp_boost")) {
                int db = constrain(_server->arg("amp_boost").toInt(), 0, 5);
                _prefs->putInt("amp_boost", db);
                setAmpBoostDb(db);
            }
            int curB = _prefs->getInt("brightness", 80);
            int curV = getCurrentAppVolume();
            int curM = getMasterVolumeLimit();
            int curAmp = getAmpBoostDb();
            String resp = "{\"ok\":true,\"brightness\":" + String(curB) +
                          ",\"volume\":" + String(curV) +
                          ",\"master_vol\":" + String(curM) +
                          ",\"amp_boost\":" + String(curAmp) + "}";
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
        _server->on("/api/snap", HTTP_ANY, [this]() {
            if (_server->hasArg("host") || _server->hasArg("addr")) {
                String h = _server->hasArg("host") ? _server->arg("host") : _server->arg("addr");
                h.trim();
                uint16_t p = 1704;
                if (h.indexOf(':') != -1) {
                    int colon = h.indexOf(':');
                    p = (uint16_t)h.substring(colon + 1).toInt();
                    h = h.substring(0, colon);
                } else if (_server->hasArg("port")) {
                    p = (uint16_t)_server->arg("port").toInt();
                }
                // Binary streaming port for Snapcast is 1704 if 1780 (control port) is entered
                uint16_t streamPort = (p == 1780) ? 1704 : (p == 0 ? 1704 : p);
                _prefs->putString("snap_host", h);
                _prefs->putInt("snap_port", streamPort);
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

        // List LittleFS stored gallery files & disk space
        _server->on("/api/gallery/files", HTTP_GET, [this]() {
            size_t total = LittleFS.totalBytes();
            size_t used  = LittleFS.usedBytes();
            String json = "{\"ok\":true,\"total\":" + String((unsigned long)total) +
                          ",\"used\":" + String((unsigned long)used) + ",\"files\":[";
            if (LittleFS.exists("/photos")) {
                File dir = LittleFS.open("/photos");
                if (dir && dir.isDirectory()) {
                    File f = dir.openNextFile();
                    bool first = true;
                    while (f) {
                        if (!f.isDirectory()) {
                            String fname = f.name();
                            int slash = fname.lastIndexOf('/');
                            if (slash >= 0) fname = fname.substring(slash + 1);
                            int bslash = fname.lastIndexOf('\\');
                            if (bslash >= 0) fname = fname.substring(bslash + 1);
                            String lower = fname;
                            lower.toLowerCase();
                            if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                                if (!first) json += ",";
                                first = false;
                                json += "{\"name\":\"" + fname + "\",\"size\":" + String((unsigned long)f.size()) + "}";
                            }
                        }
                        f = dir.openNextFile();
                    }
                    dir.close();
                }
            }
            json += "]}";
            _server->send(200, "application/json", json);
        });

        // Serve single LittleFS image for Web UI thumbnail preview
        _server->on("/api/gallery/file", HTTP_GET, [this]() {
            if (!_server->hasArg("name")) {
                _server->send(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
                return;
            }
            String name = _server->arg("name");
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            int bslash = name.lastIndexOf('\\');
            if (bslash >= 0) name = name.substring(bslash + 1);
            String fullPath = "/photos/" + name;

            if (!LittleFS.exists(fullPath)) {
                _server->send(404, "application/json", "{\"ok\":false,\"error\":\"file not found\"}");
                return;
            }
            File f = LittleFS.open(fullPath, "r");
            if (!f) {
                _server->send(500, "application/json", "{\"ok\":false,\"error\":\"open failed\"}");
                return;
            }
            _server->sendHeader("Cache-Control", "max-age=86400");
            _server->streamFile(f, "image/jpeg");
            f.close();
        });

        // Delete LittleFS image
        auto handleDelete = [this]() {
            if (!_server->hasArg("name")) {
                _server->send(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
                return;
            }
            String name = _server->arg("name");
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            int bslash = name.lastIndexOf('\\');
            if (bslash >= 0) name = name.substring(bslash + 1);
            String fullPath = "/photos/" + name;

            if (LittleFS.exists(fullPath)) {
                LittleFS.remove(fullPath);
                Serial.printf("[gallery] deleted %s\n", fullPath.c_str());
                if (galleryAppInstance && activeApp == STATE_GALLERY_UI) {
                    galleryAppInstance->reloadList();
                }
                _server->send(200, "application/json", "{\"ok\":true}");
            } else {
                _server->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
            }
        };
        _server->on("/api/gallery/delete", HTTP_GET, handleDelete);
        _server->on("/api/gallery/delete", HTTP_POST, handleDelete);

        // Upload LittleFS image (128x128 JPEG)
        _server->on("/api/gallery/upload", HTTP_POST, [this]() {
            _server->sendHeader("Connection", "close");
            _server->send(200, "application/json", "{\"ok\":true}");
            if (galleryAppInstance && activeApp == STATE_GALLERY_UI) {
                galleryAppInstance->reloadList();
            }
        }, [this]() {
            HTTPUpload& upload = _server->upload();
            static File uploadFile;
            if (upload.status == UPLOAD_FILE_START) {
                if (!LittleFS.exists("/photos")) {
                    LittleFS.mkdir("/photos");
                }
                String fname = upload.filename;
                int slash = fname.lastIndexOf('/');
                if (slash >= 0) fname = fname.substring(slash + 1);
                int bslash = fname.lastIndexOf('\\');
                if (bslash >= 0) fname = fname.substring(bslash + 1);
                if (fname.length() == 0) fname = "photo_" + String(millis()) + ".jpg";
                if (!fname.endsWith(".jpg") && !fname.endsWith(".jpeg")) fname += ".jpg";

                String path = "/photos/" + fname;
                Serial.printf("[gallery] start upload %s\n", path.c_str());
                uploadFile = LittleFS.open(path, "w");
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (uploadFile) {
                    uploadFile.write(upload.buf, upload.currentSize);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (uploadFile) {
                    uploadFile.close();
                    Serial.printf("[gallery] uploaded %u bytes successfully\n", (unsigned int)upload.totalSize);
                }
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                if (uploadFile) {
                    uploadFile.close();
                }
            }
        });

        // Media Streaming Server endpoint (Video & Audio backend)
        _server->on("/api/server", HTTP_GET, [this]() {
            if (_server->hasArg("addr")) {
                String addr = _server->arg("addr");
                addr.trim();
                if (addr.startsWith("http://")) addr = addr.substring(7);
                else if (addr.startsWith("https://")) addr = addr.substring(8);
                int slashIdx = addr.indexOf('/');
                if (slashIdx >= 0) addr = addr.substring(0, slashIdx);

                int colonIdx = addr.lastIndexOf(':');
                String hostPart = addr;
                int portPart = 8765;
                if (colonIdx > 0) {
                    hostPart = addr.substring(0, colonIdx);
                    portPart = addr.substring(colonIdx + 1).toInt();
                    if (portPart <= 0) portPart = 8765;
                }
                _prefs->putString("server_host", hostPart);
                _prefs->putInt("server_port", portPart);
            }
            if (_server->hasArg("host")) {
                _prefs->putString("server_host", _server->arg("host"));
            }
            if (_server->hasArg("port")) {
                _prefs->putInt("server_port", _server->arg("port").toInt());
            }
            String h = _prefs->getString("server_host", "192.168.0.15");
            int p = _prefs->getInt("server_port", 8765);
            _server->send(200, "application/json", "{\"ok\":true,\"addr\":\"" + h + ":" + String(p) + "\",\"host\":\"" + h + "\",\"port\":" + String(p) + "}");
        });

        // App switch
        _server->on("/api/app", HTTP_GET, [this]() {
            if (_server->hasArg("set") || _server->hasArg("name")) {
                String appName = _server->hasArg("set") ? _server->arg("set") : _server->arg("name");
                appName.toLowerCase();
                if (appName == "home" || appName == "launcher") onAppChange(STATE_LAUNCHER);
                else if (appName == "info") onAppChange(STATE_INFO);
                else if (appName == "clock") onAppChange(STATE_CLOCK);
                else if (appName == "video" || appName == "video_ui") onAppChange(STATE_VIDEO_UI);
                else if (appName == "audio" || appName == "music" || appName == "audio_ui" || appName == "music_ui") onAppChange(STATE_MUSIC_UI);
                else if (appName == "ssync" || appName == "snap" || appName == "snapclient") onAppChange(STATE_SSYNC);
                else if (appName == "gallery" || appName == "gallery_ui") onAppChange(STATE_GALLERY_UI);
                else if (appName == "settings" || appName == "settings_ui") onAppChange(STATE_SETTINGS_UI);
            } else if (_server->hasArg("state")) {
                int s = _server->arg("state").toInt();
                if (s >= 0 && s < STATE_COUNT) {
                    onAppChange((AppState)s);
                }
            }
            _server->send(200, "application/json", "{\"ok\":true,\"app\":" + String((int)activeApp) + "}");
        });

        // Playback stopped notification from streaming backend
        _server->on("/api/ui/playback_stopped", HTTP_GET, [this]() {
            if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
                videoAppInstance->onPlaybackEnded();
            } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
                musicAppInstance->onPlaybackEnded();
            }
            _server->send(200, "application/json", "{\"ok\":true}");
        });

        // NeoPixel 8-LED Ring Control (/api/pixels)
        _server->on("/api/pixels", HTTP_ANY, [this]() {
            bool changed = false;

            if (_server->hasArg("mode")) {
                String m = _server->arg("mode");
                m.toLowerCase();
                if (m == "off" || m == "0")             pixelEngine.setMode(PIXEL_MODE_OFF);
                else if (m == "solid" || m == "1")       pixelEngine.setMode(PIXEL_MODE_SOLID);
                else if (m == "spinner" || m == "2")     pixelEngine.setMode(PIXEL_MODE_SPINNER);
                else if (m == "rainbow" || m == "3")     pixelEngine.setMode(PIXEL_MODE_RAINBOW);
                else if (m == "breathe" || m == "4")     pixelEngine.setMode(PIXEL_MODE_BREATHE);
                else if (m == "fire" || m == "5")        pixelEngine.setMode(PIXEL_MODE_FIRE);
                changed = true;
            }

            if (_server->hasArg("color")) {
                String c = _server->arg("color");
                if (c.startsWith("#")) c = c.substring(1);
                long rgb = strtol(c.c_str(), NULL, 16);
                pixelEngine.setColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
                changed = true;
            } else if (_server->hasArg("r") || _server->hasArg("g") || _server->hasArg("b")) {
                uint8_t r = _server->hasArg("r") ? (uint8_t)constrain(_server->arg("r").toInt(), 0, 255) : pixelEngine.getR();
                uint8_t g = _server->hasArg("g") ? (uint8_t)constrain(_server->arg("g").toInt(), 0, 255) : pixelEngine.getG();
                uint8_t b = _server->hasArg("b") ? (uint8_t)constrain(_server->arg("b").toInt(), 0, 255) : pixelEngine.getB();
                pixelEngine.setColor(r, g, b);
                changed = true;
            }

            if (_server->hasArg("target_mask")) {
                uint8_t m = (uint8_t)constrain(_server->arg("target_mask").toInt(), 0, 255);
                pixelEngine.setTargetMask(m);
                if (pixelEngine.getMode() == PIXEL_MODE_OFF) pixelEngine.setMode(PIXEL_MODE_SOLID);
                changed = true;
            } else if (_server->hasArg("mask")) {
                uint8_t m = (uint8_t)constrain(_server->arg("mask").toInt(), 0, 255);
                pixelEngine.setTargetMask(m);
                if (pixelEngine.getMode() == PIXEL_MODE_OFF) pixelEngine.setMode(PIXEL_MODE_SOLID);
                changed = true;
            } else if (_server->hasArg("toggle_led")) {
                int k = _server->arg("toggle_led").toInt();
                if (k >= 1 && k <= 8) {
                    pixelEngine.toggleTargetLed((uint8_t)(k - 1));
                    if (pixelEngine.getMode() == PIXEL_MODE_OFF) pixelEngine.setMode(PIXEL_MODE_SOLID);
                    changed = true;
                }
            } else if (_server->hasArg("target")) {
                String t = _server->arg("target");
                if (t == "all" || t == "8") {
                    pixelEngine.setTargetMask(0xFF);
                } else {
                    int k = t.toInt();
                    if (k >= 1 && k <= 8) pixelEngine.setTargetMask(1 << (k - 1));
                    else if (k >= 0 && k < 8) pixelEngine.setTargetMask(1 << k);
                    else pixelEngine.setTargetMask(0xFF);
                }
                if (pixelEngine.getMode() == PIXEL_MODE_OFF) pixelEngine.setMode(PIXEL_MODE_SOLID);
                changed = true;
            }

            if (_server->hasArg("brightness")) {
                pixelEngine.setBrightness((uint8_t)constrain(_server->arg("brightness").toInt(), 1, 255));
                changed = true;
            }

            if (_server->hasArg("speed")) {
                pixelEngine.setSpeed((uint16_t)constrain(_server->arg("speed").toInt(), 20, 2000));
                changed = true;
            }

            if (_server->hasArg("music_light")) {
                String val = _server->arg("music_light");
                pixelEngine.setMusicLightOn(val == "1" || val == "true" || val == "on");
                changed = true;
            }

            if (_server->hasArg("music_effect")) {
                String fx = _server->arg("music_effect");
                fx.toLowerCase();
                if (fx == "auto" || fx == "0")             pixelEngine.setMusicEffect(MUSIC_FX_AUTO);
                else if (fx == "progress" || fx == "1")    pixelEngine.setMusicEffect(MUSIC_FX_PROGRESS);
                else if (fx == "red" || fx == "2")         pixelEngine.setMusicEffect(MUSIC_FX_RED);
                else if (fx == "green" || fx == "3")       pixelEngine.setMusicEffect(MUSIC_FX_GREEN);
                else if (fx == "blue" || fx == "4")        pixelEngine.setMusicEffect(MUSIC_FX_BLUE);
                else if (fx == "cyan" || fx == "5")        pixelEngine.setMusicEffect(MUSIC_FX_CYAN);
                else if (fx == "purple" || fx == "6")      pixelEngine.setMusicEffect(MUSIC_FX_PURPLE);
                else if (fx == "amber" || fx == "7")       pixelEngine.setMusicEffect(MUSIC_FX_AMBER);
                else if (fx == "rainbow" || fx == "8")     pixelEngine.setMusicEffect(MUSIC_FX_RAINBOW);
                changed = true;
            }

            if (_server->hasArg("ssync_light")) {
                String val = _server->arg("ssync_light");
                pixelEngine.setSSyncLightOn(val == "1" || val == "true" || val == "on");
                changed = true;
            }

            if (_server->hasArg("ssync_effect")) {
                String fx = _server->arg("ssync_effect");
                fx.toLowerCase();
                if (fx == "vol_hue" || fx == "0")      pixelEngine.setSSyncEffect(SSYNC_FX_VOL_HUE);
                else if (fx == "rainbow" || fx == "1") pixelEngine.setSSyncEffect(SSYNC_FX_RAINBOW);
                else if (fx == "cyan" || fx == "2")    pixelEngine.setSSyncEffect(SSYNC_FX_CYAN);
                else if (fx == "magenta" || fx == "3") pixelEngine.setSSyncEffect(SSYNC_FX_MAGENTA);
                else if (fx == "amber" || fx == "4")   pixelEngine.setSSyncEffect(SSYNC_FX_AMBER);
                changed = true;
            }

            if (_server->hasArg("freq_resp")) {
                String fr = _server->arg("freq_resp");
                fr.toLowerCase();
                if (fr == "low" || fr == "0")          pixelEngine.setFreqResponse(FREQ_RESP_LOW);
                else if (fr == "mid" || fr == "1")      pixelEngine.setFreqResponse(FREQ_RESP_MID);
                else if (fr == "high" || fr == "2")     pixelEngine.setFreqResponse(FREQ_RESP_HIGH);
                else if (fr == "all" || fr == "3")      pixelEngine.setFreqResponse(FREQ_RESP_ALL);
                changed = true;
            }

            if (changed) {
                pixelEngine.saveToPreferences(*_prefs);
            }

            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", pixelEngine.getR(), pixelEngine.getG(), pixelEngine.getB());
            char art1Buf[10], art2Buf[10];
            CRGB a1 = pixelEngine.getArtColor1();
            CRGB a2 = pixelEngine.getArtColor2();
            snprintf(art1Buf, sizeof(art1Buf), "#%02X%02X%02X", a1.r, a1.g, a1.b);
            snprintf(art2Buf, sizeof(art2Buf), "#%02X%02X%02X", a2.r, a2.g, a2.b);

            String json = "{";
            json += "\"ok\":true,";
            json += "\"mode\":" + String((int)pixelEngine.getMode()) + ",";
            json += "\"r\":" + String(pixelEngine.getR()) + ",";
            json += "\"g\":" + String(pixelEngine.getG()) + ",";
            json += "\"b\":" + String(pixelEngine.getB()) + ",";
            json += "\"color\":\"" + String(hexBuf) + "\",";
            json += "\"brightness\":" + String(pixelEngine.getBrightness()) + ",";
            json += "\"target\":" + String(pixelEngine.getTargetPixel()) + ",";
            json += "\"target_mask\":" + String((int)pixelEngine.getTargetMask()) + ",";
            json += "\"target_label\":\"" + String(pixelEngine.getTargetMaskLabel()) + "\",";
            json += "\"speed\":" + String(pixelEngine.getSpeed()) + ",";
            json += "\"music_light\":" + String(pixelEngine.getMusicLightOn() ? "true" : "false") + ",";
            json += "\"music_effect\":" + String((int)pixelEngine.getMusicEffect()) + ",";
            json += "\"ssync_light\":" + String(pixelEngine.getSSyncLightOn() ? "true" : "false") + ",";
            json += "\"ssync_effect\":" + String((int)pixelEngine.getSSyncEffect()) + ",";
            json += "\"freq_resp\":" + String((int)pixelEngine.getFreqResponse()) + ",";
            json += "\"art_color1\":\"" + String(art1Buf) + "\",";
            json += "\"art_color2\":\"" + String(art2Buf) + "\",";
            json += "\"audio_level\":" + String(pixelEngine.getAudioLevel(), 2);
            json += "}";
            _server->send(200, "application/json", json);
        });

        // WS2812B Quick LED Control (backward compatible)
        _server->on("/api/led", HTTP_GET, [this]() {
            if (_server->hasArg("r") && _server->hasArg("g") && _server->hasArg("b")) {
                uint8_t r = constrain(_server->arg("r").toInt(), 0, 255);
                uint8_t g = constrain(_server->arg("g").toInt(), 0, 255);
                uint8_t b = constrain(_server->arg("b").toInt(), 0, 255);
                pixelEngine.setColor(r, g, b);
                pixelEngine.setTargetPixel(8);
                pixelEngine.setMode(PIXEL_MODE_SOLID);
                pixelEngine.saveToPreferences(*_prefs);
            } else if (_server->hasArg("off")) {
                pixelEngine.setMode(PIXEL_MODE_OFF);
                pixelEngine.saveToPreferences(*_prefs);
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
