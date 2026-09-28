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
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoTheme.h"

// ─────────────────────────────────────────────────────────────
//  GalleryApp — Photo viewer (128×128)
//  Shows LittleFS uploaded images FIRST, then Server library images.
//  In windowed mode: 64×64 thumbnail, title, counter & hints.
//  After 2s of inactivity: switches to immersive 128×128 fullscreen.
//  Controls:
//    Single Left: Previous photo
//    Single Right: Next photo
//    Double Left: Exit fullscreen / Exit to launcher
//    Double Right: Toggle fullscreen
//  Slideshow auto-advance supported via Preferences.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class GalleryApp {
public:
    enum PhotoSource : uint8_t { PHOTO_LITTLEFS, PHOTO_SERVER };

    struct GalleryItem {
        PhotoSource source;
        char idOrPath[64];
        char title[32];
        size_t fileSize;
    };

    static constexpr int MAX_GALLERY_PHOTOS = 60;

private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool     _active          = false;
    bool     _dirty           = true;
    bool     _fullscreen      = false;
    int      _photoIdx        = 0;
    int      _photoCount      = 0;
    uint32_t _lastActivityMs  = 0;
    uint32_t _lastSlideMs     = 0;
    uint32_t _lastBlinkMs     = 0;

    GalleryItem _photos[MAX_GALLERY_PHOTOS];

    uint8_t* _imgBuf      = nullptr;
    size_t   _imgSize     = 0;
    int      _loadedIdx   = -1;
    bool     _loadFailed  = false;
    bool     _loading     = false;

    inline static Arduino_Canvas* _activeCanvas = nullptr;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (_activeCanvas) {
            _activeCanvas->draw16bitRGBBitmap(x, y, bitmap, w, h);
        } else if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

    void scanPhotos() {
        _photoCount = 0;
        _loadedIdx = -1;
        _imgSize = 0;

        // 1. Scan LittleFS FIRST (/photos/)
        if (!LittleFS.exists("/photos")) {
            LittleFS.mkdir("/photos");
        }
        File dir = LittleFS.open("/photos");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f && _photoCount < MAX_GALLERY_PHOTOS) {
                if (!f.isDirectory()) {
                    String fname = f.name();
                    int slash = fname.lastIndexOf('/');
                    if (slash >= 0) fname = fname.substring(slash + 1);
                    int bslash = fname.lastIndexOf('\\');
                    if (bslash >= 0) fname = fname.substring(bslash + 1);

                    String lower = fname;
                    lower.toLowerCase();
                    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                        GalleryItem& item = _photos[_photoCount++];
                        item.source = PHOTO_LITTLEFS;
                        snprintf(item.idOrPath, sizeof(item.idOrPath), "/photos/%s", fname.c_str());

                        // Readable title: remove .jpg extension
                        String t = fname;
                        int dot = t.lastIndexOf('.');
                        if (dot > 0) t = t.substring(0, dot);
                        strncpy(item.title, t.c_str(), sizeof(item.title) - 1);
                        item.title[sizeof(item.title) - 1] = '\0';
                        item.fileSize = f.size();
                    }
                }
                f = dir.openNextFile();
            }
            dir.close();
        }

        Serial.printf("[gallery] LittleFS photos found: %d\n", _photoCount);

        // 2. Fetch Server Images SECOND
        if (WiFi.status() == WL_CONNECTED && _photoCount < MAX_GALLERY_PHOTOS) {
            String host = prefs.getString("server_host", "192.168.0.15");
            int port = prefs.getInt("server_port", 8765);
            int remaining = MAX_GALLERY_PHOTOS - _photoCount;
            String url = "http://" + host + ":" + String(port) + "/api/library/image?page=1&page_size=" + String(remaining) + "&icons=false";

            WiFiClient client;
            HTTPClient http;
            http.begin(client, url);
            http.setTimeout(1000);
            int code = http.GET();
            if (code == 200) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, http.getStream());
                if (!err) {
                    JsonArray items = doc["items"].as<JsonArray>();
                    for (JsonObject it : items) {
                        if (_photoCount >= MAX_GALLERY_PHOTOS) break;
                        GalleryItem& item = _photos[_photoCount++];
                        item.source = PHOTO_SERVER;
                        const char* id = it["id"] | "";
                        const char* title = it["title"] | "Server Photo";
                        strncpy(item.idOrPath, id, sizeof(item.idOrPath) - 1);
                        item.idOrPath[sizeof(item.idOrPath) - 1] = '\0';
                        strncpy(item.title, title, sizeof(item.title) - 1);
                        item.title[sizeof(item.title) - 1] = '\0';
                        item.fileSize = it["size_bytes"] | 0;
                    }
                }
            }
            http.end();
            Serial.printf("[gallery] Total photos after server query: %d\n", _photoCount);
        }

        if (_photoIdx >= _photoCount) {
            _photoIdx = (_photoCount > 0) ? (_photoCount - 1) : 0;
        }
    }

    void loadCurrentPhoto() {
        if (_photoCount == 0 || _photoIdx < 0 || _photoIdx >= _photoCount) {
            _imgSize = 0;
            _loadedIdx = -1;
            _loadFailed = false;
            return;
        }

        if (_loadedIdx == _photoIdx && _imgSize > 0) {
            return; // Already in buffer
        }

        _imgSize = 0;
        _loadFailed = false;
        _loading = true;

        if (!_imgBuf) {
            _imgBuf = (uint8_t*)ps_malloc(64 * 1024);
            if (!_imgBuf) _imgBuf = (uint8_t*)malloc(64 * 1024);
        }
        if (!_imgBuf) {
            _loadFailed = true;
            _loading = false;
            return;
        }

        GalleryItem& item = _photos[_photoIdx];

        if (item.source == PHOTO_LITTLEFS) {
            if (LittleFS.exists(item.idOrPath)) {
                File f = LittleFS.open(item.idOrPath, "r");
                if (f) {
                    size_t sz = f.size();
                    if (sz > 0 && sz <= (64 * 1024)) {
                        size_t rd = f.read(_imgBuf, sz);
                        _imgSize = rd;
                        item.fileSize = rd;
                        _loadedIdx = _photoIdx;
                    }
                    f.close();
                }
            }
        } else if (item.source == PHOTO_SERVER) {
            if (WiFi.status() == WL_CONNECTED) {
                String host = prefs.getString("server_host", "192.168.0.15");
                int port = prefs.getInt("server_port", 8765);
                String url = "http://" + host + ":" + String(port) + "/api/image/" + String(item.idOrPath) + "/jpeg?size=128&aspect=square";

                WiFiClient client;
                HTTPClient http;
                http.begin(client, url);
                http.setTimeout(1000);
                int code = http.GET();
                if (code == 200) {
                    int len = http.getSize();
                    WiFiClient* stream = http.getStreamPtr();
                    size_t totalRead = 0;
                    uint32_t startMs = millis();
                    while (http.connected() && (len < 0 || totalRead < (size_t)len) && (millis() - startMs < 1000)) {
                        size_t avail = stream->available();
                        if (avail) {
                            size_t toRead = avail;
                            if (len > 0 && totalRead + toRead > (size_t)len) {
                                toRead = (size_t)len - totalRead;
                            }
                            if (totalRead + toRead > 64 * 1024) break;
                            size_t r = stream->readBytes(_imgBuf + totalRead, toRead);
                            totalRead += r;
                        } else {
                            delay(2);
                        }
                    }
                    if (totalRead > 50) {
                        _imgSize = totalRead;
                        item.fileSize = totalRead;
                        _loadedIdx = _photoIdx;
                    }
                }
                http.end();
            }
        }

        _loadFailed = (_imgSize == 0);
        _loading = false;
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        if (_photoCount == 0) {
            // Empty gallery
            _canvas->fillScreen(theme.bg);

            // Header
            _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.accent, theme.headerBg);
            _canvas->setCursor(3, 11);
            _canvas->print("Gallery");

            if (audioManager) {
                audioManager->drawStatusDot(_canvas, 44, 6, 2);
            }

            _canvas->setTextColor(theme.muted, theme.headerBg);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setCursor(104, 11);
            _canvas->print("0/0");

            // Empty state message
            _canvas->drawRoundRect(14, 22, 100, 78, 6, theme.line);
            _canvas->fillRoundRect(15, 23, 98, 76, 5, theme.surface);

            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.accent, theme.surface);
            _canvas->setCursor(34, 46);
            _canvas->print("No Photos");

            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.surface);
            _canvas->setCursor(22, 64);
            _canvas->print("Upload via Web UI");
            _canvas->setCursor(20, 76);
            _canvas->print("or connect server");

            // Footer
            _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
            _canvas->drawFastHLine(0, 114, 128, theme.line);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.footerText, theme.headerBg);
            _canvas->setCursor(44, 124);
            _canvas->print("2L:Back");

            _canvas->flush();
            return;
        }

        if (_fullscreen) {
            // Fullscreen 128×128 image only — zero borders, zero UI
            if (_imgSize > 100) {
                _activeCanvas = _canvas;
                TJpgDec.setJpgScale(1);
                TJpgDec.setSwapBytes(false);
                TJpgDec.setCallback(tftOutput);
                TJpgDec.drawJpg(0, 0, _imgBuf, _imgSize);
                _activeCanvas = nullptr;
            } else {
                _canvas->fillScreen(RGB565_BLACK);
                _canvas->setFont(u8g2_font_5x7_tf);
                _canvas->setTextColor(RGB565_WHITE, RGB565_BLACK);
                _canvas->setCursor(44, 64);
                _canvas->print("Loading...");
            }
        } else {
            // Normal Windowed mode with navigation hints
            _canvas->fillScreen(theme.bg);

            // Header (y=0..13)
            _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.accent, theme.headerBg);
            _canvas->setCursor(3, 11);
            _canvas->print("Gallery");

            if (audioManager) {
                audioManager->drawStatusDot(_canvas, 44, 6, 2);
            }

            // Source badge: [LFS] or [SRV]
            bool isLfs = (_photos[_photoIdx].source == PHOTO_LITTLEFS);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(isLfs ? 0x07E0 : 0x07FF, theme.headerBg);
            _canvas->setCursor(52, 11);
            _canvas->print(isLfs ? "[LFS]" : "[SRV]");

            // Counter (e.g. "1/8")
            char countBuf[10];
            snprintf(countBuf, sizeof(countBuf), "%d/%d", _photoIdx + 1, _photoCount);
            _canvas->setTextColor(theme.muted, theme.headerBg);
            int16_t x1, y1; uint16_t w, h;
            _canvas->getTextBounds(countBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(125 - w, 11);
            _canvas->print(countBuf);

            // Photo display frame (y=16..84)
            _canvas->drawRoundRect(30, 16, 68, 68, 4, theme.line);
            _canvas->fillRoundRect(31, 17, 66, 66, 3, theme.surface);

            if (_imgSize > 100) {
                _activeCanvas = _canvas;
                TJpgDec.setJpgScale(2);
                TJpgDec.setSwapBytes(false);
                TJpgDec.setCallback(tftOutput);
                TJpgDec.drawJpg(32, 18, _imgBuf, _imgSize);
                _activeCanvas = nullptr;
            } else if (_loadFailed) {
                _canvas->setFont(u8g2_font_5x7_tf);
                _canvas->setTextColor(0xF800, theme.surface);
                _canvas->setCursor(47, 52);
                _canvas->print("Error");
            } else {
                _canvas->setFont(u8g2_font_5x7_tf);
                _canvas->setTextColor(theme.muted, theme.surface);
                _canvas->setCursor(41, 52);
                _canvas->print("Loading");
            }

            // Subtitle / Filename (y=88..100)
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            const char* title = _photos[_photoIdx].title;
            _canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
            if (w <= 122) {
                _canvas->setCursor(64 - w / 2, 98);
            } else {
                _canvas->setCursor(3, 98);
            }
            _canvas->print(title);

            // Info line (y=102..110)
            _canvas->setFont(u8g2_font_4x6_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            char infoBuf[32];
            if (isLfs) {
                snprintf(infoBuf, sizeof(infoBuf), "LittleFS (%.1f KB)", (float)_photos[_photoIdx].fileSize / 1024.0f);
            } else {
                snprintf(infoBuf, sizeof(infoBuf), "Media Server");
            }
            _canvas->getTextBounds(infoBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 108);
            _canvas->print(infoBuf);

            // Footer (y=114..127)
            _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
            _canvas->drawFastHLine(0, 114, 128, theme.line);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.footerText, theme.headerBg);
            const char* hint = "L:Prv  R:Nxt  2L:Back";
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);
        }

        _canvas->flush();
    }

public:
    GalleryApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        if (!_imgBuf) {
            _imgBuf = (uint8_t*)ps_malloc(64 * 1024);
            if (!_imgBuf) _imgBuf = (uint8_t*)malloc(64 * 1024);
        }
    }

    void load() {
        _active          = true;
        _dirty           = true;
        _fullscreen      = false;
        _photoIdx        = 0;
        _lastActivityMs  = millis();
        _lastSlideMs     = millis();
        begin();
        scanPhotos();
        loadCurrentPhoto();
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
        if (_imgBuf) {
            free(_imgBuf);
            _imgBuf = nullptr;
        }
        _loadedIdx = -1;
        _imgSize = 0;
    }

    void reloadList() {
        if (!_active) return;
        scanPhotos();
        loadCurrentPhoto();
        _dirty = true;
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        if (_photoCount > 0) {
            _photoIdx = (_photoIdx == 0) ? (_photoCount - 1) : (_photoIdx - 1);
            loadCurrentPhoto();
        }
        _dirty = true;
    }

    void onRight() {
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        if (_photoCount > 0) {
            _photoIdx = (_photoIdx + 1) % _photoCount;
            loadCurrentPhoto();
        }
        _dirty = true;
    }

    void onBack() {
        if (_fullscreen) {
            // Exit fullscreen back to windowed mode
            _fullscreen = false;
            _lastActivityMs = millis();
            _dirty = true;
        } else {
            // In windowed mode, exit to launcher
            if (_exit) _exit(STATE_LAUNCHER);
        }
    }

    void onEnter() {
        // Toggle fullscreen mode
        _fullscreen = !_fullscreen;
        _lastActivityMs = millis();
        _dirty = true;
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();

        // Audio indicator dot blinking when not fullscreen
        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        if (audioBlinking && !_fullscreen && (now - _lastBlinkMs >= 100)) {
            _lastBlinkMs = now;
            _dirty = true;
        }

        // 2-Second Inactivity Fullscreen Transition
        if (!_fullscreen && _photoCount > 0 && (now - _lastActivityMs >= 2000)) {
            _fullscreen = true;
            _dirty = true;
        }

        // Slideshow Auto-advance Timer
        int slideInterval = prefs.getInt("gallery_timer", 0);
        if (slideInterval > 0 && _photoCount > 1 && (now - _lastSlideMs >= (uint32_t)slideInterval * 1000)) {
            _lastSlideMs = now;
            _photoIdx = (_photoIdx + 1) % _photoCount;
            loadCurrentPhoto();
            _dirty = true;
        }

        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
};
