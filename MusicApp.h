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

    static constexpr int MAX_SONGS = 32;

    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;

    bool      _active       = false;
    bool      _dirty        = true;
    bool      _paused       = false;
    MusicMode _mode         = MODE_BROWSE;

    SongItem  _songs[MAX_SONGS];
    int       _songCount    = 0;
    int       _selectedIdx  = 0;
    bool      _loadingList  = false;
    bool      _serverError  = false;

    uint8_t*  _artBuf       = nullptr;
    size_t    _artSize      = 0;
    char      _loadedId[36] = {0};

    uint32_t  _trackPos     = 0;
    uint32_t  _playStartMs  = 0;
    uint32_t  _lastSecondMs = 0;
    uint32_t  _lastDrawMs   = 0;
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

    void fetchSongList() {
        if (WiFi.status() != WL_CONNECTED) {
            _serverError = true;
            _dirty = true;
            return;
        }

        _loadingList = true;
        _serverError = false;

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/library/audio?page=1&page_size=" + String(MAX_SONGS) + "&icons=false";
        http.begin(url);
        http.setTimeout(3500);

        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getStream());
            if (!err) {
                JsonArray items = doc["items"].as<JsonArray>();
                _songCount = 0;
                for (JsonObject item : items) {
                    if (_songCount >= MAX_SONGS) break;
                    const char* id = item["id"] | "";
                    const char* title = item["title"] | "Untitled";
                    const char* artist = item["artist"] | "Unknown Artist";
                    float durF = item["duration_s"].as<float>();
                    if (durF <= 0.0f && item.containsKey("duration")) {
                        durF = item["duration"].as<float>();
                    }
                    uint32_t dur = (durF > 0.0f) ? (uint32_t)(durF + 0.5f) : 0;

                    strncpy(_songs[_songCount].id, id, sizeof(_songs[_songCount].id) - 1);
                    strncpy(_songs[_songCount].title, title, sizeof(_songs[_songCount].title) - 1);
                    strncpy(_songs[_songCount].artist, artist, sizeof(_songs[_songCount].artist) - 1);
                    _songs[_songCount].duration_s = dur;
                    _songCount++;
                }
                _serverError = (_songCount == 0);
            } else {
                _serverError = true;
            }
        } else {
            _serverError = true;
        }
        http.end();
        _loadingList = false;

        if (_songCount > 0) {
            if (_selectedIdx >= _songCount) _selectedIdx = 0;
            fetchArtwork(_selectedIdx);
        }
        _dirty = true;
    }

    void fetchArtwork(int idx) {
        if (idx < 0 || idx >= _songCount) return;
        if (strncmp(_loadedId, _songs[idx].id, sizeof(_loadedId)) == 0 && _artSize > 0) return;

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
        http.setTimeout(2500);

        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            WiFiClient* stream = http.getStreamPtr();
            size_t total = 0;
            uint32_t startWait = millis();
            while (http.connected() && (total < 16384) && (millis() - startWait < 2000)) {
                int avail = stream->available();
                if (avail > 0) {
                    int r = stream->read(_artBuf + total, min(avail, (int)(16384 - total)));
                    if (r > 0) {
                        total += r;
                        startWait = millis();
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            if (total > 100) {
                _artSize = total;
                strncpy(_loadedId, _songs[idx].id, sizeof(_loadedId) - 1);
            }
        }
        http.end();
    }

    void requestPlay(int idx, uint32_t startSec = 0) {
        if (idx < 0 || idx >= _songCount) return;

        ensureAudioOutput(44100);

        if (audioPlugin) {
            audioPlugin->stopStream();
            if (!audioPlugin->isLoaded()) {
                audioPlugin->load();
                delay(50);
            }
        }

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) +
                     "/api/audio/" + String(_songs[idx].id) + "/play?start=" + String(startSec) +
                     "&switch=false&notify=false&async=true";
        http.begin(url);
        http.setTimeout(3000);
        http.GET();
        http.end();

        _mode = MODE_PLAYING;
        _paused = false;
        _trackPos = startSec;
        _playStartMs = millis() - (startSec * 1000UL);
        _lastSecondMs = millis();
        _scrollOffset = 0;
        _lastScrollMs = millis();
        _dirty = true;

        fetchArtwork(idx);
    }

    void requestStop() {
        if (audioPlugin) {
            audioPlugin->stopStream();
        }

        HTTPClient http;
        String url = "http://" + getServerHost() + ":" + String(getServerPort()) + "/api/playback/stop?switch=false&notify=false&async=true";
        http.begin(url);
        http.setTimeout(1500);
        http.GET();
        http.end();

        _dirty = true;
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
            uint16_t statusClr = _paused ? 0xFFE0 : POKO_CLR_GREEN;
            _canvas->setTextColor(statusClr, theme.headerBg);
            _canvas->getTextBounds(statusStr, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(125 - w, 11);
            _canvas->print(statusStr);
        } else if (_songCount > 0) {
            char badge[16];
            snprintf(badge, sizeof(badge), "%d/%d", _selectedIdx + 1, _songCount);
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

            if (_artSize > 100) {
                _activeCanvas = _canvas;
                TJpgDec.setJpgScale(1);
                TJpgDec.setSwapBytes(false);
                TJpgDec.setCallback(tftOutput);
                TJpgDec.drawJpg(34, 18, _artBuf, _artSize);
                _activeCanvas = nullptr;
            } else {
                _canvas->fillRoundRect(34, 18, 60, 60, 4, theme.surface);
                _canvas->setFont(u8g2_font_helvB14_tf);
                _canvas->setTextColor(theme.accent, theme.surface);
                _canvas->setCursor(58, 54);
                _canvas->print(">");
            }

            // Single-line "Name - Artist" label (y=92) - scroll if long, else center
            _canvas->fillRect(0, 84, 128, 14, theme.bg);
            _canvas->setTextWrap(false);
            _canvas->setFont(u8g2_font_helvB08_tf);
            _canvas->setTextColor(theme.text, theme.bg);
            String label;
            if (_songCount > 0) {
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
            const char* hint = (_songCount > 0) ? "L:Prv  R:Nxt  2R:Play" : "2R:Retry  2L:Back";
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);

        } else {
            // Playing screen: Thumbnail in center, scrolling "Name - Artist", progress bar, time/vol, footer
            _canvas->drawRoundRect(32, 14, 64, 64, 4, theme.surface2);
            if (_artSize > 100) {
                _activeCanvas = _canvas;
                TJpgDec.setJpgScale(1);
                TJpgDec.setSwapBytes(false);
                TJpgDec.setCallback(tftOutput);
                TJpgDec.drawJpg(34, 16, _artBuf, _artSize);
                _activeCanvas = nullptr;
            } else {
                _canvas->fillRoundRect(34, 16, 60, 60, 3, theme.surface);
                _canvas->setFont(u8g2_font_helvB14_tf);
                _canvas->setTextColor(theme.accent, theme.surface);
                _canvas->setCursor(58, 52);
                _canvas->print(">");
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
            const char* hint = _paused ? "L:Prv  R:Nxt  2R:Resume" : "L:Prv  R:Nxt  2R:Pause";
            _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
            _canvas->setCursor(64 - w / 2, 124);
            _canvas->print(hint);
        }

        _canvas->flush();
    }

public:
    MusicApp(Arduino_GFX* gfx, AppSwitchFn exitFn)
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

        if (audioPlugin) {
            audioPlugin->load();
        }

        if (_songCount == 0) {
            fetchSongList();
        } else {
            fetchArtwork(_selectedIdx);
        }
        renderToCanvas();
    }

    void unload() {
        _active = false;
        if (_mode == MODE_PLAYING) {
            requestStop();
        }
        if (audioPlugin) {
            audioPlugin->unload();
        }
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
        if (_artBuf) {
            heap_caps_free(_artBuf);
            _artBuf = nullptr;
        }
        _artSize = 0;
        _loadedId[0] = 0;
    }

    bool isLoaded() const { return _active; }

    void onLeft() {
        if (_songCount <= 0) return;
        _selectedIdx = (_selectedIdx == 0) ? (_songCount - 1) : (_selectedIdx - 1);
        _trackPos = 0;
        _scrollOffset = 0;
        _paused = false;
        _dirty = true;
        if (_mode == MODE_PLAYING) {
            requestPlay(_selectedIdx, 0);
        } else {
            fetchArtwork(_selectedIdx);
        }
    }

    void onRight() {
        if (_songCount <= 0) return;
        _selectedIdx = (_selectedIdx + 1) % _songCount;
        _trackPos = 0;
        _scrollOffset = 0;
        _paused = false;
        _dirty = true;
        if (_mode == MODE_PLAYING) {
            requestPlay(_selectedIdx, 0);
        } else {
            fetchArtwork(_selectedIdx);
        }
    }

    void volumeRampDown() {
        int v = getCurrentAppVolume();
        if (v > 0) {
            setScaledVolume(max(0, v - 2));
            _dirty = true;
        }
    }

    void volumeRampUp() {
        int v = getCurrentAppVolume();
        if (v < 100) {
            setScaledVolume(min(100, v + 2));
            _dirty = true;
        }
    }

    void onBack() {
        _paused = false;
        if (_mode == MODE_PLAYING) {
            requestStop();
            _mode = MODE_BROWSE;
        }
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        if (_mode == MODE_PLAYING) {
            if (!_paused) {
                requestStop();
                _paused = true;
                _dirty = true;
            } else {
                requestPlay(_selectedIdx, _trackPos);
            }
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
        if (_mode == MODE_PLAYING && !_paused) {
            onRight();
        }
    }

    void update() {
        if (!_active) return;

        if (_mode == MODE_PLAYING) {
            if (!_paused) {
                if (audioPlugin && audioPlugin->hasFinished()) {
                    onPlaybackEnded();
                    return;
                }

                uint32_t now = millis();
                if (now - _lastSecondMs >= 1000) {
                    _lastSecondMs = now;
                    _trackPos++;
                    _dirty = true;

                    if (_songs[_selectedIdx].duration_s > 0 && _trackPos >= _songs[_selectedIdx].duration_s + 1) {
                        onPlaybackEnded();
                        return;
                    }
                }
            }
            uint32_t now = millis();
            if (now - _lastScrollMs >= 40) {
                _lastScrollMs = now;
                _scrollOffset++;
                _dirty = true;
            }
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

inline Arduino_Canvas* MusicApp::_activeCanvas = nullptr;
