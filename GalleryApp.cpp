#include "GalleryApp.h"

Arduino_Canvas* GalleryApp::_activeCanvas = nullptr;

bool GalleryApp::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (_activeCanvas) {
            _activeCanvas->draw16bitRGBBitmap(x, y, bitmap, w, h);
        } else if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

void GalleryApp::scanPhotos() {
        _photoCount = 0;
        _localCount = 0;
        _loadedIdx = -1;
        _imgSize = 0;
        _serverOffline = WiFi.status() != WL_CONNECTED;
        _serverError = _serverOffline;
        _contentMissing = false;

        // 1. Scan LittleFS FIRST (/photos/)
        if (!LittleFS.exists("/photos")) {
            LittleFS.mkdir("/photos");
        }
        File dir = LittleFS.open("/photos");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f && _localCount < MAX_GALLERY_PHOTOS) {
                if (!f.isDirectory()) {
                    String name = f.name();
                    name.toLowerCase();
                    if (name.endsWith(".jpg") || name.endsWith(".jpeg")) _localCount++;
                }
                f = dir.openNextFile();
            }
            dir.close();
        }
        _photoCount = _localCount;
        Serial.printf("[gallery] LittleFS photos found: %d\n", _localCount);

        // 2. Fetch Server Images SECOND
        if (WiFi.status() == WL_CONNECTED && _photoCount < MAX_GALLERY_PHOTOS) {
            String host = prefs.getString("server_host", "192.168.0.15");
            int port = prefs.getInt("server_port", 8765);
            String url = "http://" + host + ":" + String(port) + "/api/library/image?page=1&page_size=1&icons=false&enrich=false";
            WiFiClient client;
            HTTPClient http;
            http.begin(client, url);
            http.setConnectTimeout(1000);
            http.setTimeout(3000);
            esp_task_wdt_reset();
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                JsonDocument doc;
                if (!deserializeJson(doc, http.getStream())) {
                    _photoCount += max(0, min((int)(doc["total"] | 0), MAX_GALLERY_PHOTOS - _localCount));
                    _serverOffline = false;
                    _serverError = false;
                }
            } else {
                _serverOffline = code <= 0;
                _serverError = true;
            }
            http.end();
            esp_task_wdt_reset();
            Serial.printf("[gallery] Total photos after server query: %d\n", _photoCount);
        }

        if (_photoIdx >= _photoCount) {
            _photoIdx = (_photoCount > 0) ? (_photoCount - 1) : 0;
        }
    }

bool GalleryApp::selectCurrentPhoto() {
        GalleryItem selected = {};
        if (_photoIdx < _localCount) {
            File dir = LittleFS.open("/photos");
            if (!dir || !dir.isDirectory()) return false;
            File f = dir.openNextFile();
            int position = 0;
            bool found = false;
            while (f) {
                if (!f.isDirectory()) {
                    String name = f.name();
                    int slash = name.lastIndexOf('/');
                    if (slash >= 0) name = name.substring(slash + 1);
                    int bslash = name.lastIndexOf('\\');
                    if (bslash >= 0) name = name.substring(bslash + 1);
                    String lower = name;
                    lower.toLowerCase();
                    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                        if (position++ == _photoIdx) {
                            selected.source = PHOTO_LITTLEFS;
                            snprintf(selected.idOrPath, sizeof(selected.idOrPath), "/photos/%s", name.c_str());
                            // Readable title: remove .jpg extension
                            int dot = name.lastIndexOf('.');
                            if (dot > 0) name = name.substring(0, dot);
                            strlcpy(selected.title, name.c_str(), sizeof(selected.title));
                            selected.fileSize = f.size();
                            found = true;
                            break;
                        }
                    }
                }
                f = dir.openNextFile();
            }
            dir.close();
            if (!found) return false;
        } else {
            if (WiFi.status() != WL_CONNECTED) return false;
            String host = prefs.getString("server_host", "192.168.0.15");
            int port = prefs.getInt("server_port", 8765);
            String url = "http://" + host + ":" + String(port) + "/api/library/image?page=" +
                         String(_photoIdx - _localCount + 1) + "&page_size=1&icons=false&enrich=false";
            WiFiClient client;
            HTTPClient http;
            http.begin(client, url);
            http.setConnectTimeout(1000);
            http.setTimeout(3000);
            bool found = false;
            esp_task_wdt_reset();
            if (http.GET() == HTTP_CODE_OK) {
                JsonDocument doc;
                if (!deserializeJson(doc, http.getStream())) {
                    JsonArray items = doc["items"].as<JsonArray>();
                    if (!items.isNull() && items.size() > 0) {
                        JsonObject item = items[0];
                        selected.source = PHOTO_SERVER;
                        strlcpy(selected.idOrPath, item["id"] | "", sizeof(selected.idOrPath));
                        strlcpy(selected.title, item["title"] | "Server Photo", sizeof(selected.title));
                        selected.fileSize = item["size_bytes"] | 0;
                        found = selected.idOrPath[0] != '\0';
                    }
                }
            }
            http.end();
            esp_task_wdt_reset();
            if (!found) return false;
        }
        _photos[0] = selected;
        return true;
    }

void GalleryApp::loadCurrentPhoto(bool retryMissing) {
        _marquee.reset(millis());
        _titleWidth = 0;
        _lastActivityMs = millis();
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

        if (!selectCurrentPhoto()) {
            _photos[0] = {};
            strlcpy(_photos[0].title, "Unavailable", sizeof(_photos[0].title));
            _loadFailed = true;
            _loading = false;
            return;
        }
        GalleryItem& item = _photos[0];

        if (item.source == PHOTO_LITTLEFS) {
            if (LittleFS.exists(item.idOrPath)) {
                File f = LittleFS.open(item.idOrPath, "r");
                if (f) {
                    size_t sz = f.size();
                    if (sz > 4 && sz <= (64 * 1024)) {
                        size_t rd = f.read(_imgBuf, sz);
                        if (rd == sz && _imgBuf[0] == 0xFF && _imgBuf[1] == 0xD8 &&
                            _imgBuf[rd - 2] == 0xFF && _imgBuf[rd - 1] == 0xD9) {
                            _imgSize = rd;
                            item.fileSize = rd;
                            _loadedIdx = _photoIdx;
                        }
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
                http.setConnectTimeout(1000);
                http.setTimeout(1000);
                esp_task_wdt_reset();
                int code = http.GET();
                if (code == 200) {
                    _serverOffline = false;
                    _serverError = false;
                    _contentMissing = false;
                    int len = http.getSize();
                    WiFiClient* stream = http.getStreamPtr();
                    size_t totalRead = 0;
                    uint32_t lastProgressMs = millis();
                    while (http.connected() && (len < 0 || totalRead < (size_t)len) &&
                           totalRead < 64 * 1024 && millis() - lastProgressMs < 3000) {
                        size_t avail = stream->available();
                        if (avail) {
                            size_t toRead = avail;
                            if (len > 0 && totalRead + toRead > (size_t)len) {
                                toRead = (size_t)len - totalRead;
                            }
                            if (totalRead + toRead > 64 * 1024) break;
                            size_t r = stream->readBytes(_imgBuf + totalRead, toRead);
                            totalRead += r;
                            if (r > 0) lastProgressMs = millis();
                        } else {
                            delay(2);
                        }
                        esp_task_wdt_reset();
                    }
                    if (totalRead > 50 && (len < 0 || totalRead == (size_t)len) &&
                        _imgBuf[0] == 0xFF && _imgBuf[1] == 0xD8 &&
                        _imgBuf[totalRead - 2] == 0xFF && _imgBuf[totalRead - 1] == 0xD9) {
                        _imgSize = totalRead;
                        item.fileSize = totalRead;
                        _loadedIdx = _photoIdx;
                    }
                }
                http.end();
                esp_task_wdt_reset();
                if (code == HTTP_CODE_NOT_FOUND && retryMissing) {
                    // A path/config change can invalidate the cached item ID.
                    // Refresh the server count and load the closest available photo.
                    scanPhotos();
                    if (_photoCount > 0) {
                        loadCurrentPhoto(false);
                        return;
                    }
                    _serverOffline = false;
                    _serverError = false;
                    _contentMissing = true;
                } else if (code == HTTP_CODE_NOT_FOUND) {
                    _serverOffline = false;
                    _serverError = false;
                    _contentMissing = true;
                } else if (code != HTTP_CODE_OK) {
                    _serverOffline = code <= 0;
                    _serverError = true;
                    _contentMissing = false;
                }
            }
        }

        _loadFailed = (_imgSize == 0);
        _loading = false;
        // Network loading time is not time the user spent reading the title.
        _marquee.reset(millis());
        _lastActivityMs = millis();
    }

void GalleryApp::renderToCanvas() {
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
            const char* emptyTitle = _contentMissing ? "Media Not Found" :
                                     (_serverError ? (_serverOffline ? "Server Offline" : "Server Error") : "No Photos");
            int16_t ex1, ey1; uint16_t ew, eh;
            _canvas->getTextBounds(emptyTitle, 0, 0, &ex1, &ey1, &ew, &eh);
            _canvas->setCursor(max(4, 64 - (int)ew / 2), 46);
            _canvas->print(emptyTitle);

            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.surface);
            if (_serverOffline) {
                _canvas->setCursor(20, 64);
                _canvas->print("Start PoKo Server");
                _canvas->setCursor(29, 76);
                _canvas->print("then retry");
            } else {
                _canvas->setCursor(22, 64);
                _canvas->print("Upload via Web UI");
                _canvas->setCursor(30, 76);
                _canvas->print("or fix path");
            }

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
            bool isLfs = (_photos[0].source == PHOTO_LITTLEFS);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(isLfs ? pokoClrGreen() : pokoClrCyan(), theme.headerBg);
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
                const char* loadLabel = _contentMissing ? "Not Found" :
                                        (_serverOffline ? "Offline" : "Error");
                int16_t lx1, ly1; uint16_t lw, lh;
                _canvas->getTextBounds(loadLabel, 0, 0, &lx1, &ly1, &lw, &lh);
                _canvas->setCursor(64 - (int)lw / 2, 52);
                _canvas->print(loadLabel);
            } else {
                _canvas->setFont(u8g2_font_5x7_tf);
                _canvas->setTextColor(theme.muted, theme.surface);
                _canvas->setCursor(41, 52);
                _canvas->print("Loading");
            }

            // Filename ticker; the entire stored title remains readable.
            _canvas->setTextWrap(false);
            _canvas->setFont(u8g2_font_profont10_mf);
            _canvas->setTextColor(theme.text, theme.bg);
            const char* title = _photos[0].title;
            _canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
            _titleWidth = w;
            if (w <= 120) {
                _canvas->setCursor(max(4, (128 - (int)w) / 2), 98);
                _canvas->print(title);
            } else {
                int dx = 4 - _marquee.offset();
                _canvas->setCursor(dx, 98);
                _canvas->print(title);
                if (dx + (int)w < 124) {
                    _canvas->setCursor(dx + w + TitleMarquee::GAP, 98);
                    _canvas->print(title);
                }
            }
            _canvas->fillRect(0, 86, 4, 15, theme.bg);
            _canvas->fillRect(124, 86, 4, 15, theme.bg);

            // Info line (y=102..110)
            _canvas->setFont(u8g2_font_4x6_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            char infoBuf[32];
            if (isLfs) {
                snprintf(infoBuf, sizeof(infoBuf), "LittleFS (%.1f KB)", (float)_photos[0].fileSize / 1024.0f);
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

GalleryApp::GalleryApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

void GalleryApp::begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        if (!_imgBuf) {
            _imgBuf = (uint8_t*)ps_malloc(64 * 1024);
            if (!_imgBuf) _imgBuf = (uint8_t*)malloc(64 * 1024);
        }
    }

void GalleryApp::load() {
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

void GalleryApp::unload() {
        _active = false;
        if (powerManager) {
            powerManager->releaseLock(POWER_LOCK_DISPLAY, LOCK_OWNER_GALLERY);
        }
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

void GalleryApp::reloadList() {
        if (!_active) return;
        scanPhotos();
        loadCurrentPhoto();
        _dirty = true;
    }

bool GalleryApp::isLoaded() const { return _active; }

void GalleryApp::refreshTheme() { if (_active) renderToCanvas(); }

void GalleryApp::onLeft() {
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        if (_photoCount > 0) {
            _photoIdx = (_photoIdx == 0) ? (_photoCount - 1) : (_photoIdx - 1);
            loadCurrentPhoto();
        }
        _dirty = true;
    }

void GalleryApp::onRight() {
        _lastActivityMs = millis();
        _lastSlideMs    = millis();
        if (_photoCount > 0) {
            _photoIdx = (_photoIdx + 1) % _photoCount;
            loadCurrentPhoto();
        }
        _dirty = true;
    }

void GalleryApp::onBack() {
        if (_fullscreen) {
            // Exit fullscreen back to windowed mode
            _fullscreen = false;
            _marquee.reset(millis());
            _lastActivityMs = millis();
            _dirty = true;
        } else {
            // In windowed mode, exit to launcher
            if (_exit) _exit(STATE_LAUNCHER);
        }
    }

void GalleryApp::onEnter() {
        // Toggle fullscreen mode
        _fullscreen = !_fullscreen;
        _marquee.reset(millis());
        _lastActivityMs = millis();
        _dirty = true;
    }

void GalleryApp::update() {
        if (!_active) return;
        uint32_t now = millis();

        // Audio indicator dot blinking when not fullscreen
        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        if (audioBlinking && !_fullscreen && (now - _lastBlinkMs >= 100)) {
            _lastBlinkMs = now;
            _dirty = true;
        }

        if (!_fullscreen && _marquee.update(now, _titleWidth)) _dirty = true;

        // Preserve immersive viewing, but let a long title complete a pass first.
        if (!_fullscreen && _photoCount > 0 &&
            (now - _lastActivityMs >= TitleMarquee::readingTime(_titleWidth))) {
            _fullscreen = true;
            _dirty = true;
        }

        // Slideshow Auto-advance Timer
        int slideInterval = prefs.getInt("gallery_timer", 0);
        if (slideInterval > 0 && _photoCount > 1) {
            if (powerManager) powerManager->acquireLock(POWER_LOCK_DISPLAY, LOCK_OWNER_GALLERY);
            if (now - _lastSlideMs >= (uint32_t)slideInterval * 1000) {
                _lastSlideMs = now;
                _photoIdx = (_photoIdx + 1) % _photoCount;
                loadCurrentPhoto();
                _dirty = true;
            }
        } else {
            if (powerManager) powerManager->releaseLock(POWER_LOCK_DISPLAY, LOCK_OWNER_GALLERY);
        }

        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }
