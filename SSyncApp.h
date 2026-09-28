#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoPins.h"
#include "PokoDrivers.h"
#include "PokoTheme.h"
#include "SnapPlayer.h"
#include "AudioManager.h"

// ─────────────────────────────────────────────────────────────
//  SSyncApp — Direct Snapcast Client UI (128×128)
//  Connects to Snapcast server (default 192.168.0.20:1780 / 1704)
//  with client name PoKo and full ESP-IDF 5.x I2S audio playback.
// ─────────────────────────────────────────────────────────────

extern Preferences prefs;

class SSyncApp {
private:
    Arduino_GFX*    _gfx;
    AppSwitchFn     _exit;
    Arduino_Canvas* _canvas = nullptr;
    SnapPlayer*     _player = nullptr;

    bool     _active     = false;
    bool     _dirty      = true;
    uint32_t _lastDrawMs = 0;

public:
    void renderToCanvas() {
        if (!_canvas) return;
        const auto& theme = currentTheme();

        bool connected = _player ? _player->isConnected() : false;
        bool playing   = _player ? _player->isPlaying() : false;
        bool suspended = _player ? _player->isSuspended() : false;
        bool muted     = _player ? _player->isMuted() : false;
        int  volume    = _player ? _player->getVolume() : 75;
        String srvHost = _player ? _player->getServerHost() : "192.168.0.20";
        String codec   = _player ? _player->getCodec() : "FLAC/Opus";
        int32_t bufMs  = _player ? _player->getBufferMs() : 1000;
        int32_t latMs  = _player ? _player->getLatencyMs() : 0;

        _canvas->fillScreen(theme.bg);

        // Header (y=0..13)
        _canvas->fillRect(0, 0, 128, 14, theme.headerBg);
        _canvas->setFont(u8g2_font_helvB08_tf);
        _canvas->setTextColor(pokoClrGreen(), theme.headerBg);
        _canvas->setCursor(3, 11);
        _canvas->print("SSync");

        // Status badge right side
        uint16_t badgeCol = connected ? (suspended ? theme.accent : (playing ? pokoClrGreen() : theme.accent)) : POKO_CLR_ERR;
        const char* badgeText = connected ? (suspended ? "SUSP" : (playing ? "PLAY" : "IDLE")) : "OFFLINE";
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(badgeCol, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _canvas->getTextBounds(badgeText, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(125 - w, 11);
        _canvas->print(badgeText);

        // Status Card (y=18..46)
        _canvas->drawRoundRect(4, 18, 120, 30, 4, theme.line);
        _canvas->fillRoundRect(5, 19, 118, 28, 3, theme.surface);

        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.muted, theme.surface);
        _canvas->setCursor(8, 29);
        _canvas->print("Server:");
        _canvas->setTextColor(theme.text, theme.surface);
        _canvas->setCursor(50, 29);
        int srvPort = prefs.getInt("snap_port", 1704);
        String shortHost = srvHost;
        if (shortHost.startsWith("192.168.")) {
            shortHost = shortHost.substring(8);
        }
        String srv = shortHost + ":" + String(srvPort);
        if (srv.length() > 11) {
            srv = srv.substring(0, 10) + "..";
        }
        _canvas->print(srv);

        _canvas->setTextColor(theme.muted, theme.surface);
        _canvas->setCursor(8, 41);
        _canvas->print("Codec:");
        _canvas->setTextColor(theme.accent, theme.surface);
        _canvas->setCursor(50, 41);
        _canvas->print(codec);

        // Sync & Buffer Info (y=52..68)
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.muted, theme.bg);
        char bufStr[32];
        snprintf(bufStr, sizeof(bufStr), "LATENCY: %ld ms", (long)latMs);
        _canvas->setCursor(8, 58);
        _canvas->print(bufStr);

        snprintf(bufStr, sizeof(bufStr), "BUFFER:  %ld ms", (long)bufMs);
        _canvas->setCursor(8, 68);
        _canvas->print(bufStr);

        // Volume Bar (y=74..104)
        _canvas->setFont(u8g2_font_profont10_mf);
        _canvas->setTextColor(theme.text, theme.bg);
        _canvas->setCursor(8, 84);
        char volBuf[24];
        snprintf(volBuf, sizeof(volBuf), "Volume: %d%%%s", volume, muted ? " (MUTED)" : "");
        _canvas->print(volBuf);

        // Progress bar frame
        _canvas->drawRect(8, 90, 112, 10, theme.line);
        int filled = (108 * volume) / 100;
        if (filled > 0) {
            _canvas->fillRect(10, 92, filled, 6, muted ? theme.muted : pokoClrGreen());
        }

        // Footer (y=114..127)
        _canvas->fillRect(0, 114, 128, 14, theme.headerBg);
        _canvas->drawFastHLine(0, 114, 128, theme.line);
        _canvas->setFont(u8g2_font_5x7_tf);
        _canvas->setTextColor(theme.footerText, theme.headerBg);
        bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
        const char* hint = (!isSsyncActive || suspended) ? "2R:Resume SSync" : "L:V-  R:V+  2R:Mute";
        _canvas->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _canvas->setCursor(64 - w / 2, 124);
        _canvas->print(hint);

        _canvas->flush();
    }

public:
    SSyncApp(Arduino_GFX* gfx, AppSwitchFn exitFn, SnapPlayer* player = nullptr)
        : _gfx(gfx), _exit(exitFn), _player(player) {}

    void setPlayer(SnapPlayer* p) { _player = p; }

    void begin() {
        if (!_canvas) {
            _canvas = new Arduino_Canvas(128, 128, _gfx, 0, 0);
            _canvas->begin();
        }
        if (!_player) {
            _player = new SnapPlayer(nullptr, &prefs);
            _player->begin();
        }
    }

    void load() {
        _active = true;
        _dirty  = true;
        begin();
        if (_player) {
            if (!_player->isLoaded()) {
                if (audioManager && audioManager->activeSource() != AUDIO_NONE) {
                    _player->load(true);
                } else if (audioManager) {
                    audioManager->request(AUDIO_SSYNC);
                } else {
                    _player->load(false);
                }
            } else if (_player->isSuspended() && audioManager && audioManager->activeSource() == AUDIO_NONE) {
                audioManager->request(AUDIO_SSYNC);
            }
        }
        renderToCanvas();
    }

    void unload() {
        _active = false;
        // NOTE: We do NOT unload _player here! Background playback persists across app switches.
        if (_canvas) {
            delete _canvas;
            _canvas = nullptr;
        }
    }

    bool isLoaded() const { return _active; }

    bool isPlaying() const { return _player ? _player->isPlaying() : false; }

    SnapPlayer* getPlayer() { return _player; }

    void onLeft() {
        bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
        if (isSsyncActive) {
            if (audioManager) audioManager->rampVolume(-5);
        } else if (_player) {
            int curVol = _player->getVolume();
            _player->setRemoteVolumePercent(max(0, curVol - 5));
        }
        _dirty = true;
    }

    void onRight() {
        bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
        if (isSsyncActive) {
            if (audioManager) audioManager->rampVolume(5);
        } else if (_player) {
            int curVol = _player->getVolume();
            _player->setRemoteVolumePercent(min(100, curVol + 5));
        }
        _dirty = true;
    }

    void volumeRampDown() {
        bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
        if (isSsyncActive) {
            if (audioManager) audioManager->rampVolume(-2);
        } else if (_player) {
            int curVol = _player->getVolume();
            _player->setRemoteVolumePercent(max(0, curVol - 2));
        }
        _dirty = true;
    }

    void volumeRampUp() {
        bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
        if (isSsyncActive) {
            if (audioManager) audioManager->rampVolume(2);
        } else if (_player) {
            int curVol = _player->getVolume();
            _player->setRemoteVolumePercent(min(100, curVol + 2));
        }
        _dirty = true;
    }

    void onBack() {
        if (_exit) _exit(STATE_LAUNCHER);
    }

    void onEnter() {
        if (_player) {
            bool isSsyncActive = (audioManager && audioManager->activeSource() == AUDIO_SSYNC);
            if (!isSsyncActive || _player->isSuspended()) {
                if (audioManager) {
                    audioManager->request(AUDIO_SSYNC);
                }
                if (_player->isSuspended()) {
                    _player->resumeAudio();
                }
            } else {
                _player->toggleMute();
            }
            _dirty = true;
        }
    }

    void onLongRight() {
        onEnter();
    }

    void update() {
        if (!_active) return;
        uint32_t now = millis();
        if (now - _lastDrawMs >= 500) {
            _lastDrawMs = now;
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        renderToCanvas();
    }

    // REST API status
    String apiJson() {
        if (!_player) return "{\"connected\":false,\"playing\":false}";
        String j = "{";
        j += "\"connected\":" + String(_player->isConnected() ? "true" : "false") + ",";
        j += "\"playing\":" + String(_player->isPlaying() ? "true" : "false") + ",";
        j += "\"loaded\":" + String(_player->isLoaded() ? "true" : "false") + ",";
        j += "\"suspended\":" + String(_player->isSuspended() ? "true" : "false") + ",";
        j += "\"audio_active\":" + String((audioManager && audioManager->activeSource() == AUDIO_SSYNC) ? "true" : "false") + ",";
        j += "\"volume\":" + String(_player->getVolume()) + ",";
        j += "\"muted\":" + String(_player->isMuted() ? "true" : "false") + ",";
        j += "\"server\":\"" + escapeJson(_player->getServerHost() + ":" + String(_player->getServerPort())) + "\",";
        j += "\"codec\":\"" + escapeJson(_player->getCodec()) + "\",";
        j += "\"buffer_ms\":" + String(_player->getBufferMs()) + ",";
        j += "\"latency_ms\":" + String(_player->getLatencyMs());
        j += "}";
        return j;
    }
};
