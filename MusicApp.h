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
#include "PokoDrivers.h"
#include "PokoTheme.h"
#include "TCPAudio.h"
#include "AudioManager.h"

// ─────────────────────────────────────────────────────────────
//  MusicApp — Single-Song Audio Browser & MP3 TCP Stream Player
//  Displays exactly ONE song at a time with album art thumbnail,
//  title, artist, and duration.
//  Controls:
//    - L:Prv / R:Nxt (Browse songs)
//    - 2R:Play (Start MP3 TCP streaming playback over TCPAudio)
//    - While Playing: L:V- / R:V+ / 2R:Stop / 2L:Back
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;
extern TCPAudio* audioPlugin;

class MusicApp {
private:
    enum MusicMode {
        MODE_BROWSE,
        MODE_PLAYING
    };

    struct SongItem {
        char id[36];
        char title[44];
        char artist[28];
        uint32_t duration_s;
    };

    static constexpr int MAX_CATALOG_ITEMS = 999;
    static constexpr int PAGE_SIZE = 8;

    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool      _active       = false;
    bool      _dirty        = true;
    bool      _paused       = false;
    MusicMode _mode         = MODE_BROWSE;

    SongItem  _songs[PAGE_SIZE];
    int       _songCount    = 0;
    int       _selectedIdx  = 0;
    int       _pageStart    = -1;
    int       _catalogIndex = 0;
    int       _catalogTotal = 0;
    bool      _loadingList  = false;
    bool      _serverError  = false;
    bool      _serverOffline = false;
    bool      _contentMissing = false;

    uint8_t*  _artBuf       = nullptr;
    size_t    _artSize      = 0;
    char      _loadedId[36] = {0};
    uint16_t* _artBitmap    = nullptr;
    bool      _artBitmapValid = false;
    char      _artRequestedId[36] = {0};
    uint8_t   _artFailures = 0;
    uint32_t  _lastArtAttemptMs = 0;

    uint32_t  _trackPos     = 0;
    uint32_t  _playStartMs  = 0;
    uint32_t  _streamRequestMs = 0;
    bool      _streamStarted = false;
    uint32_t  _lastSecondMs = 0;
    uint32_t  _lastDrawMs   = 0;
    int       _scrollOffset = 0;
    uint32_t  _lastScrollMs = 0;

    static uint16_t* _decodeTarget;
    static int16_t   _decodeTargetW;
    static int16_t   _decodeTargetH;
    static bool      _needsColorExtract;

    static bool tftDecodeBitmap(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (_decodeTarget) {
            for (int16_t j = 0; j < h; j++) {
                int16_t dstY = y + j;
                if (dstY < 0 || dstY >= _decodeTargetH) continue;
                for (int16_t i = 0; i < w; i++) {
                    int16_t dstX = x + i;
                    if (dstX < 0 || dstX >= _decodeTargetW) continue;
                    _decodeTarget[dstY * _decodeTargetW + dstX] = bitmap[j * w + i];
                }
            }
        }
        if (_needsColorExtract) {
            pixelEngine.samplePixels(bitmap, (size_t)w * (size_t)h);
        }
        return true;
    }

    void decodeArtworkToBitmap() {
        if (_artSize <= 100 || !_artBuf) {
            _artBitmapValid = false;
            return;
        }

        if (!_artBitmap) {
            if (psramFound()) {
                _artBitmap = (uint16_t*)heap_caps_malloc(60 * 60 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!_artBitmap) {
                _artBitmap = (uint16_t*)malloc(60 * 60 * sizeof(uint16_t));
            }
        }
        if (!_artBitmap) {
            _artBitmapValid = false;
            return;
        }

        memset(_artBitmap, 0, 60 * 60 * sizeof(uint16_t));
        _decodeTarget = _artBitmap;
        _decodeTargetW = 60;
        _decodeTargetH = 60;
        _needsColorExtract = true;
        pixelEngine.startColorExtraction();
        TJpgDec.setJpgScale(1);
        TJpgDec.setSwapBytes(false);
        TJpgDec.setCallback(tftDecodeBitmap);
        JRESULT decoded = TJpgDec.drawJpg(0, 0, _artBuf, _artSize);
        pixelEngine.finishColorExtraction();
        _needsColorExtract = false;
        _decodeTarget = nullptr;
        _artBitmapValid = (decoded == JDR_OK);
    }

    String getServerHost() {
        return prefs.getString("server_host", "192.168.0.15");
    }

    int getServerPort() {
        return prefs.getInt("server_port", 8765);
    }

    bool fetchSongList(int index = 0, bool loadArt = true) {
        if (WiFi.status() != WL_CONNECTED || index < 0 || index >= MAX_CATALOG_ITEMS) {
            _serverError = true;
            _serverOffline = true;
            _contentMissing = false;
            _dirty = true;
            return false;
        }
        if (_songCount > 0 && index >= _pageStart && index < _pageStart + _songCount) {
            _catalogIndex = index;
            _selectedIdx = index - _pageStart;
            if (loadArt) fetchArtwork(_selectedIdx);
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
                     "/api/library/audio?page=" + String(start / PAGE_SIZE + 1) +
                     "&page_size=" + String(PAGE_SIZE) + "&icons=false";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(5000);
        int httpCode = http.GET();
        bool parsed = false;
        int total = 0, count = 0;
        SongItem pending[PAGE_SIZE] = {};
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getStream())) {
                parsed = true;
                total = min((int)(doc["total"] | 0), MAX_CATALOG_ITEMS);
                JsonArray items = doc["items"].as<JsonArray>();
                for (JsonObject item : items) {
                    if (count >= PAGE_SIZE || start + count >= total) break;
                    SongItem& song = pending[count];
                    strlcpy(song.id, item["id"] | "", sizeof(song.id));
                    strlcpy(song.title, item["title"] | "Untitled", sizeof(song.title));
                    strlcpy(song.artist, item["artist"] | "Unknown Artist", sizeof(song.artist));
                    float duration = item["duration_s"].as<float>();
                    if (duration <= 0 && item.containsKey("duration")) duration = item["duration"].as<float>();
                    song.duration_s = duration > 0 ? (uint32_t)(duration + 0.5f) : 0;
                    if (!song.id[0]) break;
                    count++;
                }
            }
        }
        http.end();
        _loadingList = false;
        if (parsed && total == 0) {
            _songCount = 0;
            _catalogTotal = 0;
            _catalogIndex = 0;
            _pageStart = -1;
        }
        bool loaded = index >= start && index < start + count;
        if (loaded) {
            memcpy(_songs, pending, count * sizeof(SongItem));
            _songCount = count;
            _pageStart = start;
            _selectedIdx = index - start;
            _catalogIndex = index;
            _catalogTotal = total;
            if (loadArt) fetchArtwork(_selectedIdx);
        }
        _serverError = !parsed;
        _serverOffline = httpCode <= 0;
        _contentMissing = parsed && total > 0 && !loaded;
        _dirty = true;
        return loaded;
    }

    void fetchArtwork(int idx) {
        if (idx < 0 || idx >= _songCount) return;
        if (strncmp(_artRequestedId, _songs[idx].id, sizeof(_artRequestedId)) != 0) {
            strlcpy(_artRequestedId, _songs[idx].id, sizeof(_artRequestedId));
            _artFailures = 0;
        }
        if (strncmp(_loadedId, _songs[idx].id, sizeof(_loadedId)) == 0 && _artSize > 0 && _artBitmapValid) return;
        _lastArtAttemptMs = millis();
        _artBitmapValid = false;
        _loadedId[0] = '\0';
        _artSize = 0;
        _dirty = true;

        if (!_artBuf) {
            if (psramFound()) {
                _artBuf = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!_artBuf) {
                _artBuf = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
        }
        if (!_artBuf) return;

        _artSize = 0;
        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/library/audio/" + String(_songs[idx].id) + "/thumbnail.jpg?size=60";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(1000);

        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            WiFiClient* stream = http.getStreamPtr();
            int expected = http.getSize();
            size_t total = 0;
            uint32_t startWait = millis();
            while (http.connected() && (total < 16384) &&
                   (expected < 0 || total < (size_t)expected) && (millis() - startWait < 3000)) {
                int avail = stream->available();
                if (avail > 0) {
                    int limit = min(avail, (int)(16384 - total));
                    if (expected > 0) limit = min(limit, expected - (int)total);
                    int r = stream->read(_artBuf + total, limit);
                    if (r > 0) {
                        total += r;
                        startWait = millis();
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            if (total > 100 && (expected < 0 || total == (size_t)expected) &&
                _artBuf[0] == 0xFF && _artBuf[1] == 0xD8 &&
                _artBuf[total - 2] == 0xFF && _artBuf[total - 1] == 0xD9) {
                _artSize = total;
                decodeArtworkToBitmap();
                if (_artBitmapValid) strlcpy(_loadedId, _songs[idx].id, sizeof(_loadedId));
            }
        }
        http.end();
        if (!_artBitmapValid) _artFailures++;
    }

    bool requestPlay(int idx, uint32_t startSec = 0, bool retryMissing = true) {
        if (idx < 0 || idx >= _songCount) return false;
        bool wasMusicActive = audioManager && audioManager->activeSource() == AUDIO_MUSIC;

        if (!audioManager || !audioManager->request(AUDIO_MUSIC)) {
            if (!wasMusicActive) _mode = MODE_BROWSE;
            _serverError = true;
            _serverOffline = false;
            _contentMissing = false;
            _dirty = true;
            return false;
        }

        if (!ensureAudioOutput(44100)) {
            if (wasMusicActive) requestStop();
            else audioManager->release(AUDIO_MUSIC);
            _mode = MODE_BROWSE;
            _serverError = true;
            _serverOffline = false;
            _contentMissing = false;
            _dirty = true;
            return false;
        }

        if (audioPlugin) {
            audioPlugin->stopStream();
            if (!audioPlugin->isLoaded()) {
                audioPlugin->load();
                delay(50);
            }
        }
        if (!audioPlugin || !audioPlugin->isLoaded()) {
            if (wasMusicActive) requestStop();
            else audioManager->release(AUDIO_MUSIC);
            _mode = MODE_BROWSE;
            _serverError = true;
            _serverOffline = false;
            _contentMissing = false;
            _dirty = true;
            return false;
        }

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/audio/" + String(_songs[idx].id) + "/play?start=" + String(startSec) +
                     "&switch=false&notify=false";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(5000);
        int httpCode = http.GET();
        JsonDocument response;
        bool started = httpCode == HTTP_CODE_OK &&
                       !deserializeJson(response, http.getStream()) && response["ok"].as<bool>();
        http.end();

        if (started) {
            _selectedIdx = idx;
            _mode = MODE_PLAYING;
            _paused = false;
            _trackPos = startSec;
            _playStartMs = millis() - (startSec * 1000UL);
            _streamRequestMs = millis();
            _streamStarted = false;
            _lastSecondMs = millis();
            _scrollOffset = 0;
            _lastScrollMs = millis();
            _dirty = true;
            _serverError = false;
            _serverOffline = false;
            _contentMissing = false;

            if (_songs[idx].duration_s > 0) {
                pixelEngine.setSongProgress((float)startSec / (float)_songs[idx].duration_s);
            } else {
                pixelEngine.setSongProgress(0.0f);
            }

            fetchArtwork(idx);
            return true;
        }
        Serial.printf("[music] requestPlay failed with code %d\n", httpCode);
        _mode = MODE_BROWSE;
        _paused = false;
        _trackPos = 0;
        _serverError = httpCode != HTTP_CODE_NOT_FOUND;
        _serverOffline = httpCode <= 0;
        _contentMissing = httpCode == HTTP_CODE_NOT_FOUND;
        _dirty = true;
        audioPlugin->stopStream();
        audioPlugin->unload();
        if (!wasMusicActive) audioManager->release(AUDIO_MUSIC);
        if (httpCode == HTTP_CODE_NOT_FOUND && retryMissing) {
            int target = _catalogIndex;
            _songCount = 0;
            _pageStart = -1;
            if (!fetchSongList(target, false) && target > 0) {
                fetchSongList(0, false);
            }
            if (_songCount > 0) return requestPlay(_selectedIdx, 0, false);
            _serverError = false;
            _serverOffline = false;
            _contentMissing = true;
            _dirty = true;
        }
        return false;
    }

    void requestStop() {
        if (audioPlugin) {
            audioPlugin->stopStream();
        }

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/playback/stop?switch=false&notify=false";
        http.begin(url);
        http.setConnectTimeout(1000);
        http.setTimeout(4000);
        http.GET();
        http.end();

        pixelEngine.setSongProgress(0.0f);
        _dirty = true;
    }

    bool selectSong(int index) {
        SongItem previous = _songs[_selectedIdx];
        int previousIndex = _catalogIndex;
        int previousTotal = _catalogTotal;
        if (!fetchSongList(index, false)) return false;
        if (_mode == MODE_PLAYING) {
            if (requestPlay(_selectedIdx)) return true;
            _songs[0] = previous;
            _songCount = 1;
            _pageStart = previousIndex;
            _catalogIndex = previousIndex;
            _catalogTotal = previousTotal;
            _selectedIdx = 0;
            _dirty = true;
            return false;
        }
        _trackPos = 0;
        _scrollOffset = 0;
        _paused = false;
        fetchArtwork(_selectedIdx);
        return true;
    }

    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        _canvas->fillScreen(theme.bg);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(theme.accent, theme.headerBg);
        _canvas->setCursor(3, 11);
        _canvas->print("Music");

        // Counter / status
        _canvas->setFont(u8g2_font_5x7_tf);
        int16_t x1, y1; uint16_t w, h;

        if (_mode == MODE_PLAYING) {
            const char* statusStr = _paused ? "PAUSED" : "PLAYING";
            uint16_t statusClr = _paused ? pokoClrWarn() : pokoClrGreen();
            _canvas->setTextColor(statusClr, theme.headerBg);
            _canvas->getTextBounds(statusStr, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(125 - w, 11);
            _canvas->print(statusStr);
        } else if (_serverError || _contentMissing) {
            _canvas->setTextColor(0xF800, theme.headerBg);
            _canvas->setCursor(76, 11);
            _canvas->print(_contentMissing ? "Missing" : (_serverOffline ? "Offline" : "Error"));
        } else if (_songCount > 0) {
            char badge[24];
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

        if (_mode == MODE_BROWSE) {
            // Album Artwork Frame (y=16..78)
            _canvas->drawRoundRect(32, 16, 64, 64, 6, theme.surface2);

            if (!_serverError && !_contentMissing && _artBitmapValid && _artBitmap) {
                _canvas->draw16bitRGBBitmap(34, 18, _artBitmap, 60, 60);
            } else {
                _canvas->fillRoundRect(34, 18, 60, 60, 4, theme.surface);
                _canvas->setFont(u8g2_font_helvB14_tf);
                bool showError = _serverError || _contentMissing;
                _canvas->setTextColor(showError ? 0xF800 : theme.accent, theme.surface);
                _canvas->setCursor(showError ? 61 : 58, 54);
                _canvas->print(showError ? "!" : ">");
            }

            // Single-line "Name - Artist" label (y=92) - scroll if long, else center
            _canvas->fillRect(0, 84, 128, 14, theme.bg);
            _canvas->setTextWrap(false);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            String label;
            if (_contentMissing) {
                label = "Media Not Found";
            } else if (_serverError) {
                label = _serverOffline ? "Start PoKo Server" : "Server Error";
            } else if (_songCount > 0) {
                if (strlen(_songs[_selectedIdx].artist) > 0) {
                    label = String(_songs[_selectedIdx].title) + " - " + String(_songs[_selectedIdx].artist);
                } else {
                    label = String(_songs[_selectedIdx].title);
                }
            } else {
                label = _serverError ? "Start PoKo Server" : "No Songs";
            }
            int16_t x1, y1; uint16_t tw, th;
            _canvas->getTextBounds(label.c_str(), 0, 0, &x1, &y1, &tw, &th);
            if (tw <= 120) {
                _canvas->setCursor(max(4, (128 - (int)tw) / 2), 92);
                _canvas->print(label);
            } else {
                int loopLen = tw + 32;
                int offset = _scrollOffset % loopLen;
                int dx = 4 - offset;
                _canvas->setCursor(dx, 92);
                _canvas->print(label);
                if (dx + tw < 124) {
                    _canvas->setCursor(dx + loopLen, 92);
                    _canvas->print(label);
                }
            }

            // Duration (y=105)
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            char infoBuf[32];
            if (_songCount > 0) {
                uint32_t dur = _songs[_selectedIdx].duration_s;
                if (dur > 0) {
                    snprintf(infoBuf, sizeof(infoBuf), "%02lu:%02lu", (unsigned long)(dur / 60), (unsigned long)(dur % 60));
                } else {
                    snprintf(infoBuf, sizeof(infoBuf), "--:--");
                }
            } else {
                snprintf(infoBuf, sizeof(infoBuf), "%s:%d", getServerHost().c_str(), getServerPort());
            }
            _canvas->getTextBounds(infoBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - (int)w / 2, 105);
            _canvas->print(infoBuf);

            // Footer (y=114..127)
            _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
            _canvas->drawFastHLine(0, 114, 128, theme.line);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.footerText, theme.headerBg);
            const char* hint = (_serverError || _contentMissing) ? "2R:Retry  2L:Back" :
                               ((_songCount > 0) ? "L:Prv  R:Nxt  2R:Play" : "2R:Retry  2L:Back");
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);

        } else {
            // Playing screen: Thumbnail in center, scrolling "Name - Artist", progress bar, time/vol, footer
            _canvas->drawRoundRect(32, 14, 64, 64, 4, theme.surface2);
            if (!_serverError && _artBitmapValid && _artBitmap) {
                _canvas->draw16bitRGBBitmap(34, 16, _artBitmap, 60, 60);
            } else {
                _canvas->fillRoundRect(34, 16, 60, 60, 3, theme.surface);
                _canvas->setFont(u8g2_font_helvB14_tf);
                _canvas->setTextColor(_serverError ? 0xF800 : theme.accent, theme.surface);
                _canvas->setCursor(_serverError ? 61 : 58, 52);
                _canvas->print(_serverError ? "!" : ">");
            }

            // Scrolling "Name - Artist" ticker (y=80..93) - strictly ONE line, no wrapping
            _canvas->fillRect(0, 80, 128, 14, theme.bg);
            _canvas->setTextWrap(false);
            String ticker = String(_songs[_selectedIdx].title) + " - " + String(_songs[_selectedIdx].artist);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            int16_t x1, y1; uint16_t tw, th;
            _canvas->getTextBounds(ticker.c_str(), 0, 0, &x1, &y1, &tw, &th);
            if (tw <= 120) {
                _canvas->setCursor(max(4, (128 - tw) / 2), 90);
                _canvas->print(ticker);
            } else {
                int loopLen = tw + 32;
                int offset = _scrollOffset % loopLen;
                int dx = 4 - offset;
                _canvas->setCursor(dx, 90);
                _canvas->print(ticker);
                if (dx + tw < 124) {
                    _canvas->setCursor(dx + loopLen, 90);
                    _canvas->print(ticker);
                }
            }

            // Progress Bar (y=96..100)
            _canvas->drawRect(14, 96, 100, 5, theme.line);
            uint32_t dur = (_songs[_selectedIdx].duration_s > 0) ? _songs[_selectedIdx].duration_s : 0;
            int progW = (dur > 0) ? min(96, (int)((96 * _trackPos) / dur)) : 0;
            if (progW > 0) {
                _canvas->fillRect(16, 97, progW, 3, theme.accent);
            }

            // Time & Volume (y=103..111)
            char timeBuf[32];
            if (dur > 0) {
                snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / %02lu:%02lu  V:%d%%",
                         (unsigned long)(_trackPos / 60), (unsigned long)(_trackPos % 60),
                         (unsigned long)(dur / 60), (unsigned long)(dur % 60), getCurrentAppVolume());
            } else {
                snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu  V:%d%%",
                         (unsigned long)(_trackPos / 60), (unsigned long)(_trackPos % 60),
                         getCurrentAppVolume());
            }
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.muted, theme.bg);
            _canvas->getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 110);
            _canvas->print(timeBuf);

            // Footer (y=114..127)
            _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
            _canvas->drawFastHLine(0, 114, 128, theme.line);
            _canvas->setFont(u8g2_font_5x7_tf);
            _canvas->setTextColor(theme.footerText, theme.headerBg);
            const char* hint = _paused ? "2R:Resume  2L:Back" : "L:Prv  R:Nxt  2R:Pause";
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);
        }

        _canvas->flush();
    }

public:
    MusicApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
        : _gfx(gfx), _exit(exitFn) {}

    ~MusicApp() {
        if (_artBuf) {
            if (psramFound()) heap_caps_free(_artBuf);
            else free(_artBuf);
            _artBuf = nullptr;
        }
        if (_artBitmap) {
            if (psramFound()) heap_caps_free(_artBitmap);
            else free(_artBitmap);
            _artBitmap = nullptr;
        }
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
    }

    void pausePlayback() {
        if (_mode == MODE_PLAYING && !_paused) {
            if (audioPlugin) {
                audioPlugin->stopStream();
            }
            HTTPClient http;
            String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/playback/stop?switch=false&notify=false";
            http.begin(url);
            http.setConnectTimeout(1000);
            http.setTimeout(4000);
            http.GET();
            http.end();
            _paused = true;
            _dirty = true;
        }
    }

    void resumePlayback() {
        if (_mode == MODE_PLAYING && _paused) {
            requestPlay(_selectedIdx, _trackPos);
        }
    }

    void stopPlaybackInternal() {
        requestStop();
        if (audioPlugin) {
            audioPlugin->unload();
        }
        _mode = MODE_BROWSE;
        _paused = false;
        _trackPos = 0;
        _dirty = true;
    }

    void stopPlayback() {
        stopPlaybackInternal();
        if (audioManager) {
            audioManager->release(AUDIO_MUSIC);
        }
    }

    void togglePlayPause() {
        if (_mode == MODE_PLAYING) {
            if (_paused) {
                resumePlayback();
            } else {
                pausePlayback();
            }
        }
    }

    bool prepareRemoteStream() {
        if (!audioPlugin || !audioManager || !audioManager->request(AUDIO_MUSIC)) return false;
        if (!ensureAudioOutput(44100)) {
            audioManager->release(AUDIO_MUSIC);
            return false;
        }
        audioPlugin->stopStream();
        if (!audioPlugin->isLoaded()) audioPlugin->load();
        if (!audioPlugin->isLoaded()) {
            audioManager->release(AUDIO_MUSIC);
            return false;
        }
        _streamRequestMs = millis();
        _streamStarted = false;
        return true;
    }

    void load() {
        _active = true;
        _dirty  = true;
        begin();

        if (_mode == MODE_PLAYING) {
            // Already playing in background! Keep playing, just refresh UI!
            if (!_artBitmapValid && _artSize > 100) {
                decodeArtworkToBitmap();
            }
            renderToCanvas();
            return;
        }

        _mode = MODE_BROWSE;

        _pageStart = -1;
        if (!fetchSongList(_catalogIndex) && _catalogIndex > 0) fetchSongList(0);
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_mode != MODE_PLAYING) {
            if (audioManager) {
                audioManager->release(AUDIO_MUSIC);
            }
            if (audioPlugin) {
                audioPlugin->unload();
            }
        }
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    bool isLoaded() const { return _active; }

    void refreshTheme() { if (_active) renderToCanvas(); }

    void onLeft() {
        if (_catalogTotal <= 0) return;
        selectSong((_catalogIndex == 0) ? (_catalogTotal - 1) : (_catalogIndex - 1));
    }

    void onRight() {
        if (_catalogTotal <= 0) return;
        selectSong((_catalogIndex + 1) % _catalogTotal);
    }

    void volumeRampDown(int step = 2) {
        if (audioManager) audioManager->rampVolume(-step);
        else {
            int v = getCurrentAppVolume();
            if (v > 0) setScaledVolume(max(0, v - step));
        }
        _dirty = true;
    }

    void volumeRampUp(int step = 2) {
        if (audioManager) audioManager->rampVolume(step);
        else {
            int v = getCurrentAppVolume();
            if (v < 100) setScaledVolume(min(100, v + step));
        }
        _dirty = true;
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        if (_mode == MODE_PLAYING) {
            togglePlayPause();
            return;
        }
        if (_songCount > 0) {
            _paused = false;
            requestPlay(_selectedIdx, 0);
        } else {
            fetchSongList();
        }
    }

    void onPlaybackEnded() {
        if (_mode == MODE_PLAYING && !_paused && _catalogTotal > 0 &&
            !selectSong((_catalogIndex + 1) % _catalogTotal)) {
            requestStop();
            _mode = MODE_BROWSE;
            _dirty = true;
        }
    }

    void update() {
        if (_mode == MODE_PLAYING && !_paused) {
            if (!_streamStarted && audioPlugin && audioPlugin->isConnected()) _streamStarted = true;
            if (!_streamStarted && millis() - _streamRequestMs > 10000) {
                requestStop();
                if (audioPlugin) audioPlugin->unload();
                _mode = MODE_BROWSE;
                _serverError = true;
                _dirty = true;
                return;
            }
            if (audioPlugin && audioPlugin->hasFinished()) {
                onPlaybackEnded();
                return;
            }

            uint32_t now = millis();
            if (now - _lastSecondMs >= 1000) {
                _lastSecondMs = now;
                _trackPos++;
                _dirty = true;

                if (_songs[_selectedIdx].duration_s > 0) {
                    float prog = (float)_trackPos / (float)_songs[_selectedIdx].duration_s;
                    pixelEngine.setSongProgress(prog);
                }

                if (_songs[_selectedIdx].duration_s > 0 && _trackPos >= _songs[_selectedIdx].duration_s + 1) {
                    onPlaybackEnded();
                    return;
                }
            }
        }

        if (!_active || !_canvas) return;

        uint32_t now = millis();
        if (!_artBitmapValid && _songCount > 0 && _artFailures < 3 &&
            WiFi.status() == WL_CONNECTED && now - _lastArtAttemptMs >= 5000) {
            fetchArtwork(_selectedIdx);
        }
        if (now - _lastScrollMs >= 40) {
            _lastScrollMs = now;
            _scrollOffset++;
            _dirty = true;
        }

        if (_dirty) {
            _dirty = false;
            renderToCanvas();
        }
    }

    bool isPlaying() const {
        return (_mode == MODE_PLAYING) && !_paused;
    }

    const char* getCurrentTitle() const {
        if (_songCount > 0 && _selectedIdx >= 0 && _selectedIdx < _songCount) {
            return _songs[_selectedIdx].title;
        }
        return "Music";
    }

    bool hasServerError() const {
        return _serverError;
    }
};

inline uint16_t* MusicApp::_decodeTarget = nullptr;
inline int16_t   MusicApp::_decodeTargetW = 0;
inline int16_t   MusicApp::_decodeTargetH = 0;
inline bool      MusicApp::_needsColorExtract = false;

