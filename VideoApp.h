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
#include "PokoTheme.h"
#include "PokoDrivers.h"
#include "SyncedAVPlayer.h"

// ─────────────────────────────────────────────────────────────
//  VideoApp — Single-Thumbnail Video Browser & Synced AV Player
//  Displays exactly ONE video thumbnail on screen at a time with
//  title and duration.
//  Controls:
//    - L:Prv / R:Nxt (Browse videos)
//    - 2R:Play (Start 128×128 synced video stream)
//    - While Playing: 2R:Stop / 2L:Back / Hold: Volume
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern SyncedAVPlayer* syncPlugin;

class VideoApp {
private:
    enum VideoMode {
        MODE_BROWSE,
        MODE_PLAYING
    };

    struct VideoItem {
        char id[36];
        char title[44];
        uint32_t duration_s;
    };

    static constexpr int MAX_VIDEOS = 32;

    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool      _active       = false;
    bool      _dirty        = true;
    VideoMode _mode         = MODE_BROWSE;

    VideoItem _videos[MAX_VIDEOS];
    int       _videoCount   = 0;
    int       _selectedIdx  = 0;
    bool      _loadingList  = false;
    bool      _serverError  = false;

    uint8_t*  _thumbBuf     = nullptr;
    size_t    _thumbSize    = 0;
    char      _loadedId[36] = {0};

    uint32_t  _lastDrawMs   = 0;
    uint32_t  _playStartMs  = 0;
    int       _scrollOffset = 0;
    uint32_t  _lastScrollMs = 0;

    static Arduino_Canvas* _activeCanvas;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (_activeCanvas) {
            _activeCanvas->draw16bitRGBBitmap(x, y, bitmap, w, h);
        } else if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

    String getServerHost() {
        return prefs.getString("server_host", "192.168.0.15");
    }

    int getServerPort() {
        return prefs.getInt("server_port", 8765);
    }

    void fetchVideoList() {
        if (WiFi.status() != WL_CONNECTED) {
            _serverError = true;
            _dirty = true;
            return;
        }

        _loadingList = true;
        _serverError = false;

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/library/video?page=1&page_size=" + String(MAX_VIDEOS) + "&icons=false";
        http.begin(url);
        http.setTimeout(3500);

        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getStream());
            if (!err) {
                JsonArray items = doc["items"].as<JsonArray>();
                _videoCount = 0;
                for (JsonObject item : items) {
                    if (_videoCount >= MAX_VIDEOS) break;
                    const char* id = item["id"] | "";
                    const char* title = item["title"] | "Untitled";
                    float durF = item["duration_s"].as<float>();
                    if (durF <= 0.0f && item.containsKey("duration")) {
                        durF = item["duration"].as<float>();
                    }
                    uint32_t dur = (durF > 0.0f) ? (uint32_t)(durF + 0.5f) : 0;

                    strncpy(_videos[_videoCount].id, id, sizeof(_videos[_videoCount].id) - 1);
                    strncpy(_videos[_videoCount].title, title, sizeof(_videos[_videoCount].title) - 1);
                    _videos[_videoCount].duration_s = dur;
                    _videoCount++;
                }
                _serverError = (_videoCount == 0);
            } else {
                _serverError = true;
            }
        } else {
            _serverError = true;
        }
        http.end();
        _loadingList = false;

        if (_videoCount > 0) {
            if (_selectedIdx >= _videoCount) _selectedIdx = 0;
            fetchThumbnail(_selectedIdx);
        }
        _dirty = true;
    }

    void fetchThumbnail(int idx) {
        if (idx < 0 || idx >= _videoCount) return;
        if (strncmp(_loadedId, _videos[idx].id, sizeof(_loadedId)) == 0 && _thumbSize > 0) return;

        if (!_thumbBuf) {
            if (psramFound()) {
                _thumbBuf = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!_thumbBuf) {
                _thumbBuf = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
        }
        if (!_thumbBuf) return;

        _thumbSize = 0;
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/library/video/" + String(_videos[idx].id) + "/thumbnail.jpg?size=80";
        http.begin(url);
        http.setTimeout(2500);

        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            WiFiClient* stream = http.getStreamPtr();
            size_t total = 0;
            uint32_t startWait = millis();
            while (http.connected() && (total < 16384) && (millis() - startWait < 2000)) {
                int avail = stream->available();
                if (avail > 0) {
                    int r = stream->read(_thumbBuf + total, min(avail, (int)(16384 - total)));
                    if (r > 0) {
                        total += r;
                        startWait = millis();
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            if (total > 100) {
                _thumbSize = total;
                strncpy(_loadedId, _videos[idx].id, sizeof(_loadedId) - 1);
            }
        }
        http.end();
    }

    void requestPlay(int idx) {
        if (idx < 0 || idx >= _videoCount) return;

        ensureAudioOutput(44100);

        if (syncPlugin) {
            syncPlugin->reset();
            if (!syncPlugin->isLoaded()) {
                syncPlugin->load();
                delay(50);
            }
        }

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/video/" + String(_videos[idx].id) +
                     "/play?audio=true&aspect=square&profile=balanced&start=0&switch=false&notify=false&async=true";
        http.begin(url);
        http.setTimeout(3000);
        http.GET();
        http.end();

        _mode = MODE_PLAYING;
        _playStartMs = millis();
        _dirty = true;

        fetchThumbnail(idx);
    }

    void requestStop() {
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/playback/stop?switch=false&notify=false&async=true";
        http.begin(url);
        http.setTimeout(1500);
        http.GET();
        http.end();

        if (syncPlugin) {
            syncPlugin->reset();
        }

        _mode = MODE_BROWSE;
        _dirty = true;
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        _canvas->fillScreen(theme.bg);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(0x001F, theme.headerBg);
        _canvas->setCursor(3, 11);
        _canvas->print("Video");

        // Counter / status badge
        _canvas->setFont(u8g2_font_5x7_tf);
        int16_t x1, y1; uint16_t w, h;

        if (_videoCount > 0) {
            char badge[16];
            snprintf(badge, sizeof(badge), "%d/%d", _selectedIdx + 1, _videoCount);
            _canvas->setTextColor(theme.muted, theme.headerBg);
            _canvas->getTextBounds(badge, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(125 - w, 11);
            _canvas->print(badge);
        } else if (_loadingList) {
            _canvas->setTextColor(theme.muted, theme.headerBg);
            _canvas->setCursor(72, 11);
            _canvas->print("Loading...");
        } else {
            _canvas->setTextColor(0xF800, theme.headerBg);
            _canvas->setCursor(76, 11);
            _canvas->print("Offline");
        }

        // Single Video Card Frame (y=16..84)
        _canvas->drawRoundRect(14, 16, 100, 68, 6, theme.surface2);

        if (_thumbSize > 100) {
            // Draw downloaded JPEG thumbnail centered inside the card
            _activeCanvas = _canvas;
            TJpgDec.setJpgScale(1);
            TJpgDec.setSwapBytes(false);
            TJpgDec.setCallback(tftOutput);
            TJpgDec.drawJpg(24, 18, _thumbBuf, _thumbSize);
            _activeCanvas = nullptr;
        } else {
            // Placeholder video slate
            _canvas->fillRoundRect(16, 18, 96, 64, 4, theme.surface);
            _canvas->setFont(u8g2_font_helvB14_tf);
            _canvas->setTextColor(theme.accent, theme.surface);
            _canvas->setCursor(58, 56);
            _canvas->print(">");
        }

        // Title (y=92..101) - scroll if long, else center
        _canvas->fillRect(0, 88, 128, 14, theme.bg);
        _canvas->setTextWrap(false);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.text, theme.bg);
        const char* title = (_videoCount > 0) ? _videos[_selectedIdx].title : (_serverError ? "Start PoKo Server" : "No Videos");
        _canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        if (w <= 120) {
            _canvas->setCursor(max(4, (128 - (int)w) / 2), 94);
            _canvas->print(title);
        } else {
            int loopLen = w + 32;
            int offset = _scrollOffset % loopLen;
            int dx = 4 - offset;
            _canvas->setCursor(dx, 94);
            _canvas->print(title);
            if (dx + (int)w < 124) {
                _canvas->setCursor(dx + loopLen, 94);
                _canvas->print(title);
            }
        }

        // Subtitle / Duration (y=102..112)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.muted, theme.bg);
        char subBuf[32];
        if (_videoCount > 0) {
            uint32_t dur = _videos[_selectedIdx].duration_s;
            if (dur > 0) {
                snprintf(subBuf, sizeof(subBuf), "%02lu:%02lu  128x128", (unsigned long)(dur / 60), (unsigned long)(dur % 60));
            } else {
                snprintf(subBuf, sizeof(subBuf), "--:--  128x128");
            }
        } else {
            snprintf(subBuf, sizeof(subBuf), "%s:%d", getServerHost().c_str(), getServerPort());
        }
        _canvas->getTextBounds(subBuf, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(max(4, 64 - w / 2), 106);
        _canvas->print(subBuf);

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, 114, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = (_videoCount > 0) ? "L:Prv  R:Nxt  2R:Play" : "2R:Retry  2L:Back";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    VideoApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void load() {
        _active = true;
        _dirty  = true;
        _mode   = MODE_BROWSE;
        begin();

        ensureAudioOutput(44100);

        if (syncPlugin) {
            syncPlugin->load();
        }

        if (_videoCount == 0) {
            fetchVideoList();
        } else {
            fetchThumbnail(_selectedIdx);
        }
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_mode == MODE_PLAYING) {
            requestStop();
        }
        if (syncPlugin) {
            syncPlugin->unload();
        }
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
        if (_thumbBuf) {
            heap_caps_free(_thumbBuf);
            _thumbBuf = nullptr;
        }
        _thumbSize = 0;
        _loadedId[0] = 0;
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        if (_videoCount <= 0) return;
        _selectedIdx = (_selectedIdx == 0) ? (_videoCount - 1) : (_selectedIdx - 1);
        _scrollOffset = 0;
        _lastScrollMs = millis();
        _dirty = true;
        if (_mode == MODE_PLAYING) {
            requestPlay(_selectedIdx);
        } else {
            fetchThumbnail(_selectedIdx);
        }
    }

    void onRight() {
        if (_videoCount <= 0) return;
        _selectedIdx = (_selectedIdx + 1) % _videoCount;
        _scrollOffset = 0;
        _lastScrollMs = millis();
        _dirty = true;
        if (_mode == MODE_PLAYING) {
            requestPlay(_selectedIdx);
        } else {
            fetchThumbnail(_selectedIdx);
        }
    }

    void volumeRampDown() {
        int v = getCurrentAppVolume();
        if (v > 0) setScaledVolume(max(0, v - 2));
    }

    void volumeRampUp() {
        int v = getCurrentAppVolume();
        if (v < 100) setScaledVolume(min(100, v + 2));
    }

    void onBack() {
        if (_mode == MODE_PLAYING) {
            requestStop();
        }
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        if (_mode == MODE_PLAYING) {
            requestStop();
            return;
        }
        if (_videoCount > 0) {
            requestPlay(_selectedIdx);
        } else {
            fetchVideoList();
        }
    }

    void onPlaybackEnded() {
        if (_mode == MODE_PLAYING) {
            onRight();
        }
    }

    void update() {
        if (!_active) return;

        if (_mode == MODE_PLAYING) {
            if (syncPlugin) {
                syncPlugin->update();
                if (syncPlugin->hasFinished()) {
                    onPlaybackEnded();
                    return;
                }
            }
            if (_videos[_selectedIdx].duration_s > 0) {
                if (millis() - _playStartMs > (_videos[_selectedIdx].duration_s + 2) * 1000UL) {
                    onPlaybackEnded();
                    return;
                }
            }
            return;
        } else {
            uint32_t now = millis();
            if (now - _lastScrollMs >= 40) {
                _lastScrollMs = now;
                _scrollOffset++;
                _dirty = true;
            }
        }

        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
};

inline Arduino_Canvas* VideoApp::_activeCanvas = nullptr;
