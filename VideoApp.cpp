#include "VideoApp.h"

Arduino_Canvas* VideoApp::_activeCanvas = nullptr;
VideoApp* VideoApp::_instance = nullptr;

bool VideoApp::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (_activeCanvas) {
            _activeCanvas->draw16bitRGBBitmap(x, y, bitmap, w, h);
        } else if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

String VideoApp::getServerHost() {
        return prefs.getString("server_host", "192.168.0.15");
    }

int VideoApp::getServerPort() {
        return prefs.getInt("server_port", 8765);
    }

bool VideoApp::fetchVideoList(int index, bool loadThumb) {
        if (WiFi.status() != WL_CONNECTED || index < 0 || index >= MAX_CATALOG_ITEMS) {
            _serverError = true;
            _serverOffline = true;
            _contentMissing = false;
            _dirty = true;
            return false;
        }
        if (_videoCount > 0 && index >= _pageStart && index < _pageStart + _videoCount) {
            _catalogIndex = index;
            _selectedIdx = index - _pageStart;
            if (loadThumb) fetchThumbnail(_selectedIdx);
            _serverError = false;
            _serverOffline = false;
            _contentMissing = false;
            _dirty = true;
            return true;
        }

        _loadingList = true;
        int start = (index / PAGE_SIZE) * PAGE_SIZE;
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/library/video?page=" + String(start / PAGE_SIZE + 1) +
                     "&page_size=" + String(PAGE_SIZE) + "&icons=false";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(5000);
        int httpCode = http.GET();
        bool parsed = false;
        int total = 0, count = 0;
        VideoItem pending[PAGE_SIZE] = {};
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getStream())) {
                parsed = true;
                total = min((int)(doc["total"] | 0), MAX_CATALOG_ITEMS);
                JsonArray items = doc["items"].as<JsonArray>();
                for (JsonObject item : items) {
                    if (count >= PAGE_SIZE || start + count >= total) break;
                    VideoItem& video = pending[count];
                    strlcpy(video.id, item["id"] | "", sizeof(video.id));
                    strlcpy(video.title, item["title"] | "Untitled", sizeof(video.title));
                    float duration = item["duration_s"].as<float>();
                    if (duration <= 0 && item.containsKey("duration")) duration = item["duration"].as<float>();
                    video.duration_s = duration > 0 ? (uint32_t)(duration + 0.5f) : 0;
                    if (!video.id[0]) break;
                    count++;
                }
            }
        }
        http.end();
        _loadingList = false;
        if (parsed && total == 0) {
            _videoCount = 0;
            _catalogTotal = 0;
            _catalogIndex = 0;
            _pageStart = -1;
        }
        bool loaded = index >= start && index < start + count;
        if (loaded) {
            memcpy(_videos, pending, count * sizeof(VideoItem));
            _videoCount = count;
            _pageStart = start;
            _selectedIdx = index - start;
            _catalogIndex = index;
            _catalogTotal = total;
            if (loadThumb) fetchThumbnail(_selectedIdx);
        }
        _serverError = !parsed;
        _serverOffline = httpCode <= 0;
        _contentMissing = parsed && total > 0 && !loaded;
        _dirty = true;
        return loaded;
    }

void VideoApp::fetchThumbnail(int idx) {
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
        http.setConnectTimeout(1000);
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
                strlcpy(_loadedId, _videos[idx].id, sizeof(_loadedId));
            }
        }
        http.end();
        _marquee.reset(millis());
    }

void VideoApp::failPlayback(int httpCode) {
        if (syncPlugin) syncPlugin->unload();
        // A timed-out worker retains its buffers and ownership until it exits.
        if (audioManager && (!syncPlugin || !syncPlugin->isLoaded())) {
            audioManager->release(AUDIO_VIDEO);
        }
        if (powerManager) {
            powerManager->releaseLock(POWER_LOCK_DISPLAY | POWER_LOCK_REALTIME_NET, LOCK_OWNER_VIDEO);
        }
        _mode = MODE_BROWSE;
        _serverError = true;
        _serverOffline = httpCode <= 0;
        _contentMissing = false;
        _streamStarted = false;
        _dirty = true;
    }

void VideoApp::requestPlay(int idx, bool retryMissing) {
        if (idx < 0 || idx >= _videoCount) return;
        // Quiesce BOTH old transports and the audio consumer before clearing
        // their queues/clock. A live reset mixes old timestamps into the new clip.
        if (syncPlugin) syncPlugin->unload();
        if (!syncPlugin || syncPlugin->isLoaded()) {
            failPlayback();
            return;
        }
        if (!audioManager || !audioManager->request(AUDIO_VIDEO)) {
            failPlayback();
            return;
        }
        if (!ensureAudioOutput(44100)) {
            failPlayback();
            return;
        }
        syncPlugin->load();
        if (!syncPlugin->isLoaded() || !syncPlugin->isRunning()) {
            failPlayback();
            return;
        }
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/video/" + String(_videos[idx].id) +
                     "/play?audio=true&aspect=square&profile=balanced&start=0&switch=false&notify=false&async=true";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(3000);
        int httpCode = http.GET();
        http.end();
        if (httpCode >= 200 && httpCode < 300) {
            _mode = MODE_PLAYING;
            _serverError = false;
            _serverOffline = false;
            _contentMissing = false;
            _streamStarted = false;
            _playStartMs = millis();
            _dirty = true;
            // Never fetch a thumbnail while incoming frames need draining.
            if (powerManager) {
                powerManager->acquireLock(POWER_LOCK_DISPLAY | POWER_LOCK_REALTIME_NET, LOCK_OWNER_VIDEO);
            }
        } else {
            Serial.printf("[video] requestPlay failed with code %d\n", httpCode);
            failPlayback(httpCode);
            if (httpCode == HTTP_CODE_NOT_FOUND && retryMissing) {
                // The server is reachable, but this cached ID disappeared after a
                // path/config change. Refresh the catalog and play the item now at
                // the same position (or the first available item if it shrank).
                int target = _catalogIndex;
                _videoCount = 0;
                _pageStart = -1;
                if (!fetchVideoList(target, false) && target > 0) {
                    fetchVideoList(0, false);
                }
                if (_videoCount > 0) {
                    requestPlay(_selectedIdx, false);
                    return;
                }
                _serverError = false;
                _serverOffline = false;
                _contentMissing = true;
                _dirty = true;
            } else if (httpCode == HTTP_CODE_NOT_FOUND) {
                _serverError = false;
                _serverOffline = false;
                _contentMissing = true;
                _dirty = true;
            }
        }
    }

void VideoApp::requestStopInternal() {
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/playback/stop?switch=false&notify=false&async=true";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(1500);
        http.GET();
        http.end();

        if (syncPlugin) {
            syncPlugin->unload();
        }

        if (powerManager) {
            powerManager->releaseLock(POWER_LOCK_DISPLAY | POWER_LOCK_REALTIME_NET, LOCK_OWNER_VIDEO);
        }

        _mode = MODE_BROWSE;
        _dirty = true;
    }

void VideoApp::requestStop() {
        requestStopInternal();
        if (audioManager && (!syncPlugin || !syncPlugin->isLoaded())) {
            audioManager->release(AUDIO_VIDEO);
        }
        if (_videoCount > 0) fetchThumbnail(_selectedIdx);
    }

bool VideoApp::selectVideo(int index) {
        VideoItem previous = _videos[_selectedIdx];
        int previousIndex = _catalogIndex;
        int previousTotal = _catalogTotal;
        if (!fetchVideoList(index, false)) return false;
        _marquee.reset(millis());
        _titleWidth = 0;
        if (_mode == MODE_PLAYING) {
            requestPlay(_selectedIdx);
            if (_mode == MODE_PLAYING) return true;
            _videos[0] = previous;
            _videoCount = 1;
            _pageStart = previousIndex;
            _catalogIndex = previousIndex;
            _catalogTotal = previousTotal;
            _selectedIdx = 0;
            _dirty = true;
            return false;
        }
        fetchThumbnail(_selectedIdx);
        return true;
    }

void VideoApp::renderToCanvas() {
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

        if (_serverError || _contentMissing) {
            _canvas->setTextColor(0xF800, theme.headerBg);
            _canvas->setCursor(76, 11);
            _canvas->print(_contentMissing ? "Missing" : (_serverOffline ? "Offline" : "Error"));
        } else if (_videoCount > 0) {
            char badge[16];
            snprintf(badge, sizeof(badge), "%d/%d", _catalogIndex + 1, _catalogTotal);
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

        if (!_serverError && !_contentMissing && _thumbSize > 100) {
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
            bool showError = _serverError || _contentMissing;
            _canvas->setTextColor(showError ? 0xF800 : theme.accent, theme.surface);
            _canvas->setCursor(showError ? 61 : 58, 56);
            _canvas->print(showError ? "!" : ">");
        }

        // Title (y=92..101) - scroll if long, else center
        _canvas->fillRect(0, 88, 128, 14, theme.bg);
        _canvas->setTextWrap(false);
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.text, theme.bg);
        const char* title = _contentMissing ? "Media Not Found" :
                            (_serverError ? (_serverOffline ? "Start PoKo Server" : "Server Error") :
                            ((_videoCount > 0) ? _videos[_selectedIdx].title : "No Videos"));
        _canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        _titleWidth = w;
        if (w <= 120) {
            _canvas->setCursor(max(4, (128 - (int)w) / 2), 94);
            _canvas->print(title);
        } else {
            int loopLen = w + 32;
            int offset = _marquee.offset();
            int dx = 4 - offset;
            _canvas->setCursor(dx, 94);
            _canvas->print(title);
            if (dx + (int)w < 124) {
                _canvas->setCursor(dx + loopLen, 94);
                _canvas->print(title);
            }
        }

        _canvas->fillRect(0, 86, 4, 16, theme.bg);
        _canvas->fillRect(124, 86, 4, 16, theme.bg);

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
        const char* hint = (_serverError || _contentMissing) ? "Failed  2R:Retry" : ((_videoCount > 0) ? "L:Prv  R:Nxt  2R:Play" : "2R:Retry  2L:Back");
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

VideoApp::VideoApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {
        _instance = this;
    }

bool VideoApp::playbackStoppedStatic() {
        return !syncPlugin || !syncPlugin->isLoaded();
    }

void VideoApp::stopPlaybackStatic() {
        if (_instance) _instance->requestStopInternal();
    }

void VideoApp::begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        if (audioManager) {
            audioManager->setVideoHandlers(stopPlaybackStatic, playbackStoppedStatic);
        }
    }

void VideoApp::load() {
        _active = true;
        _dirty  = true;
        _mode   = MODE_BROWSE;
        _marquee.reset(millis());
        _titleWidth = 0;
        begin();

        _pageStart = -1;
        if (!fetchVideoList(_catalogIndex) && _catalogIndex > 0) fetchVideoList(0);
        renderToCanvas();
    }

bool VideoApp::prepareRemoteStream() {
        if (!syncPlugin) return false;
        syncPlugin->unload();
        if (syncPlugin->isLoaded() || !audioManager || !audioManager->request(AUDIO_VIDEO)) return false;
        if (!ensureAudioOutput(44100)) {
            audioManager->release(AUDIO_VIDEO);
            return false;
        }
        syncPlugin->load();
        if (!syncPlugin->isLoaded() || !syncPlugin->isRunning()) {
            failPlayback();
            return false;
        }
        _mode = MODE_PLAYING;
        _streamStarted = false;
        _playStartMs = millis();
        _serverError = false;
        _serverOffline = false;
        _contentMissing = false;
        _dirty = true;
        if (powerManager) powerManager->acquireLock(POWER_LOCK_DISPLAY | POWER_LOCK_REALTIME_NET, LOCK_OWNER_VIDEO);
        return true;
    }

void VideoApp::unload() {
        _active = false;
        if (powerManager) {
            powerManager->releaseLock(POWER_LOCK_DISPLAY | POWER_LOCK_REALTIME_NET, LOCK_OWNER_VIDEO);
        }
        if (_mode == MODE_PLAYING) {
            requestStop();
        } else {
            if (syncPlugin) {
                syncPlugin->unload();
            }
            if (audioManager && (!syncPlugin || !syncPlugin->isLoaded())) {
                audioManager->release(AUDIO_VIDEO);
            }
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

bool VideoApp::isLoaded() const { return _active; }

void VideoApp::refreshTheme() { if (_active && _mode != MODE_PLAYING) renderToCanvas(); }

void VideoApp::onLeft() {
        if (_catalogTotal <= 0) return;
        selectVideo((_catalogIndex == 0) ? (_catalogTotal - 1) : (_catalogIndex - 1));
    }

void VideoApp::onRight() {
        if (_catalogTotal <= 0) return;
        selectVideo((_catalogIndex + 1) % _catalogTotal);
    }

void VideoApp::volumeRampDown(int step) {
        if (audioManager) audioManager->rampVolume(-step);
        else {
            int v = getCurrentAppVolume();
            if (v > 0) setScaledVolume(max(0, v - step));
        }
    }

void VideoApp::volumeRampUp(int step) {
        if (audioManager) audioManager->rampVolume(step);
        else {
            int v = getCurrentAppVolume();
            if (v < 100) setScaledVolume(min(100, v + step));
        }
    }

void VideoApp::onBack() {
        if (_mode == MODE_PLAYING) {
            requestStop();
        }
        if (_exit) _exit(STATE_LAUNCHER);
    }

void VideoApp::onEnter() {
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

void VideoApp::onPlaybackEnded() {
        if (_mode == MODE_PLAYING && _catalogTotal > 0 &&
            !selectVideo((_catalogIndex + 1) % _catalogTotal)) {
            requestStop();
        }
    }

void VideoApp::update() {
        if (!_active) return;

        if (_mode == MODE_PLAYING) {
            if (syncPlugin) {
                syncPlugin->update();
                if (!_streamStarted && syncPlugin->hasStarted()) {
                    _streamStarted = true;
                    _playStartMs = millis();
                }
                if (syncPlugin->hasFinished()) {
                    onPlaybackEnded();
                    return;
                }
            }
            if (!_streamStarted && millis() - _playStartMs > 10000) {
                Serial.println("[video] stream startup timed out");
                failPlayback();
                return;
            }
            if (_streamStarted && _videos[_selectedIdx].duration_s > 0) {
                if (millis() - _playStartMs > (_videos[_selectedIdx].duration_s + 2) * 1000UL) {
                    onPlaybackEnded();
                    return;
                }
            }
            return;
        } else {
            uint32_t now = millis();
            if (_marquee.update(now, _titleWidth)) _dirty = true;
        }

        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }
