#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include "PokoAppState.h"
#include "PokoDrivers.h"
#include "PokoTheme.h"
#include "SnapPlayer.h"

// ─────────────────────────────────────────────────────────────
//  AudioManager — Centralized audio session & output management
//  Ensures exclusive I2S access, suspends/resumes background SSync,
//  and renders the global "Now Playing" overlay.
// ─────────────────────────────────────────────────────────────

enum AudioSource {
    AUDIO_NONE = 0,
    AUDIO_SSYNC,
    AUDIO_MUSIC,
    AUDIO_VIDEO,
    AUDIO_BLUETOOTH
};

typedef void (*AudioActionFn)();
typedef bool (*AudioQueryFn)();
typedef const char* (*AudioStrFn)();

class AudioManager {
private:
    AudioSource _activeSource    = AUDIO_NONE;
    AudioSource _suspendedSource = AUDIO_NONE;
    SnapPlayer* _snapPlayer      = nullptr;
    AudioActionFn _musicStopFn    = nullptr;
    AudioActionFn _musicToggleFn  = nullptr;
    AudioQueryFn  _musicIsPlayingFn = nullptr;
    AudioStrFn    _musicGetTitleFn = nullptr;
    AudioQueryFn  _musicErrorFn = nullptr;

    bool _overlayOpen = false;

public:
    AudioManager() {}

    void setSnapPlayer(SnapPlayer* p) { _snapPlayer = p; }
    void setMusicHandlers(AudioActionFn stopFn, AudioActionFn toggleFn, AudioQueryFn isPlayingFn, AudioStrFn getTitleFn = nullptr, AudioQueryFn errorFn = nullptr) {
        _musicStopFn = stopFn;
        _musicToggleFn = toggleFn;
        _musicIsPlayingFn = isPlayingFn;
        _musicGetTitleFn = getTitleFn;
        _musicErrorFn = errorFn;
    }

    AudioSource activeSource() const { return _activeSource; }
    AudioSource suspendedSource() const { return _suspendedSource; }

    bool request(AudioSource source) {
        if (source == AUDIO_NONE) {
            stopAll();
            return true;
        }

        Serial.printf("[audioMgr] request source %d (active=%d, suspended=%d)\n",
                      (int)source, (int)_activeSource, (int)_suspendedSource);

        if (source == AUDIO_SSYNC) {
            // Stop older music playback if active
            if (_musicStopFn) {
                _musicStopFn();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            _activeSource = AUDIO_SSYNC;
            _suspendedSource = AUDIO_NONE;
            if (_snapPlayer) {
                if (!_snapPlayer->isLoaded()) {
                    _snapPlayer->load();
                } else {
                    _snapPlayer->resumeAudio();
                }
            }
            return true;
        }

        if (source == AUDIO_MUSIC) {
            // Unload background SSync completely to eliminate Wi-Fi & CPU contention
            if (_snapPlayer && _snapPlayer->isLoaded()) {
                _snapPlayer->unload();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            _suspendedSource = AUDIO_NONE;
            _activeSource = AUDIO_MUSIC;
            return true;
        }

        if (source == AUDIO_VIDEO) {
            if (_snapPlayer && _snapPlayer->isLoaded()) {
                _snapPlayer->unload();
            }
            if (_musicStopFn) {
                _musicStopFn();
            }
            _suspendedSource = AUDIO_NONE;
            _activeSource = AUDIO_VIDEO;
            return true;
        }

        _activeSource = source;
        return true;
    }

    void release(AudioSource source) {
        if (_activeSource != source) return;

        Serial.printf("[audioMgr] release source %d\n", (int)source);
        _activeSource = AUDIO_NONE;
        _suspendedSource = AUDIO_NONE;
    }

    bool hasActiveSession() const {
        return (_activeSource != AUDIO_NONE) || (_snapPlayer && _snapPlayer->isLoaded());
    }

    void stopActiveSession() {
        Serial.printf("[audioMgr] stopActiveSession (active=%d)\n", (int)_activeSource);
        AudioSource cur = _activeSource;
        _activeSource = AUDIO_NONE;
        _suspendedSource = AUDIO_NONE;
        if ((cur == AUDIO_MUSIC || cur == AUDIO_NONE) && _musicStopFn) {
            _musicStopFn();
        }
        if ((cur == AUDIO_SSYNC || cur == AUDIO_NONE) && _snapPlayer && _snapPlayer->isLoaded()) {
            _snapPlayer->stop();
            _snapPlayer->unload();
        }
        closeOverlay();
    }

    void stopAll() {
        Serial.println("[audioMgr] stopAll");
        _suspendedSource = AUDIO_NONE;
        _activeSource = AUDIO_NONE;
        if (_snapPlayer) {
            _snapPlayer->stop();
            _snapPlayer->unload();
        }
        if (_musicStopFn) {
            _musicStopFn();
        }
    }

    bool isPlaying() const {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            return _snapPlayer->isPlaying() && !_snapPlayer->isSuspended();
        }
        if (_activeSource == AUDIO_MUSIC) {
            if (_musicIsPlayingFn) return _musicIsPlayingFn();
            return true;
        }
        if (_activeSource == AUDIO_VIDEO) {
            return true;
        }
        return false;
    }

    void togglePlayPause() {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            if (_snapPlayer->isSuspended()) {
                _snapPlayer->resumeAudio();
            } else {
                _snapPlayer->toggleMute();
            }
        } else if (_activeSource == AUDIO_MUSIC && _musicToggleFn) {
            _musicToggleFn();
        }
    }

    const char* getSourceName() const {
        switch (_activeSource) {
            case AUDIO_SSYNC: return "SSync";
            case AUDIO_MUSIC: return "Music";
            case AUDIO_VIDEO: return "Video";
            case AUDIO_BLUETOOTH: return "Bluetooth";
            default: return "Idle";
        }
    }

    bool hasError() const {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            return _snapPlayer->isLoaded() && !_snapPlayer->isConnected();
        }
        if (_activeSource == AUDIO_MUSIC && _musicErrorFn) {
            return _musicErrorFn();
        }
        return false;
    }

    bool isSoundPlaying() const {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            return _snapPlayer->isPlaying() && !_snapPlayer->isSuspended() && !_snapPlayer->isMuted();
        }
        if (_activeSource == AUDIO_MUSIC && _musicIsPlayingFn) {
            return _musicIsPlayingFn();
        }
        if (_activeSource == AUDIO_VIDEO) {
            return true;
        }
        return false;
    }

    bool isSessionActive() const {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            return _snapPlayer->isLoaded();
        }
        if (_activeSource == AUDIO_MUSIC) {
            return true;
        }
        if (_activeSource == AUDIO_VIDEO) {
            return true;
        }
        return false;
    }

    uint16_t getSourceColor() const {
        switch (_activeSource) {
            case AUDIO_SSYNC: return 0x07FF; // Cyan
            case AUDIO_MUSIC: return 0xF81F; // Pink
            case AUDIO_VIDEO: return 0x001F; // Blue
            default: return 0;
        }
    }

    void drawStatusDot(Arduino_Canvas* canvas, int16_t x, int16_t y, int16_t r = 2) {
        if (!canvas || _activeSource == AUDIO_NONE) return;

        bool blinkPhase = ((millis() / 300) % 2 == 0);
        uint16_t color = getSourceColor();
        bool show = false;

        if (hasError()) {
            color = 0xF800; // Red
            show = blinkPhase;
        } else if (isSoundPlaying()) {
            show = blinkPhase; // Blinking when actively playing sound
        } else if (isSessionActive()) {
            show = true; // Solid when active but silent / paused / idle
        }

        if (show && color != 0) {
            canvas->fillCircle(x, y, r, color);
        }
    }

    // ── Now Playing Overlay ──────────────────────────────────
    bool isOverlayOpen() const { return _overlayOpen; }
    void openOverlay() { _overlayOpen = true; }
    void closeOverlay() { _overlayOpen = false; }
    void toggleOverlay() { _overlayOpen = !_overlayOpen; }

    void renderOverlay(Arduino_GFX* gfx) {
        if (!gfx) return;
        const auto& theme = currentTheme();

        // Backdrop card
        gfx->fillRoundRect(4, 4, 120, 120, 6, theme.surface);
        gfx->drawRoundRect(4, 4, 120, 120, 6, theme.surface2);

        // Header (y=4..19)
        gfx->fillRoundRect(5, 5, 118, 15, 4, theme.headerBg);
        gfx->setFont(u8g2_font_5x7_tf);
        gfx->setTextColor(theme.accent, theme.headerBg);
        gfx->setCursor(8, 15);
        gfx->print("NOW PLAYING");

        // Close hint
        gfx->setTextColor(theme.muted, theme.headerBg);
        gfx->setCursor(86, 15);
        gfx->print("2L:Back");

        // Source Title (y=38)
        gfx->setFont(u8g2_font_helvB10_tf);
        gfx->setTextColor(theme.text, theme.surface);
        const char* sName = getSourceName();
        int16_t x1, y1; uint16_t w, h;
        gfx->getTextBounds(sName, 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(max(6, (int)(64 - w / 2)), 38);
        gfx->print(sName);

        // Subtitle / Info (y=52)
        gfx->setFont(u8g2_font_profont10_mf);
        gfx->setTextColor(theme.muted, theme.surface);
        String info = "";
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            info = _snapPlayer->getServerHost();
        } else if (_activeSource == AUDIO_MUSIC) {
            info = _musicGetTitleFn ? _musicGetTitleFn() : "Local Audio";
        } else if (_activeSource == AUDIO_VIDEO) {
            info = "Video Stream";
        } else {
            info = "No audio session";
        }
        gfx->getTextBounds(info.c_str(), 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(max(8, (int)(64 - w / 2)), 52);
        gfx->print(info.c_str());

        // Status Badge (y=62..80)
        bool playing = isPlaying();
        bool muted = (_activeSource == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isMuted());
        uint16_t badgeBg = playing ? 0x03E0 : 0xFD20;
        gfx->fillRoundRect(20, 63, 88, 18, 4, badgeBg);
        gfx->setFont(u8g2_font_helvB08_tf);
        gfx->setTextColor(0xFFFF, badgeBg);
        const char* badgeText = playing ? "▶  PLAYING" : (muted ? "M  MUTED" : "❚❚  IDLE");
        gfx->getTextBounds(badgeText, 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(max(22, (int)(64 - w / 2)), 75);
        gfx->print(badgeText);

        // Volume bar (y=88..95)
        int vol = getCurrentAppVolume();
        gfx->drawRect(16, 88, 96, 7, theme.line);
        int fillW = (92 * vol) / 100;
        if (fillW > 0) {
            gfx->fillRect(18, 90, fillW, 3, theme.accent);
        }

        // Footer (y=104..123)
        gfx->fillRect(5, 104, 118, 19, theme.headerBg);
        gfx->drawFastHLine(5, 104, 118, theme.line);
        gfx->setFont(u8g2_font_5x7_tf);
        gfx->setTextColor(theme.footerText, theme.headerBg);
        gfx->setCursor(8, 117);
        if (_activeSource == AUDIO_SSYNC) {
            gfx->print(muted ? "L:Unmute" : "L:Mute");
        } else {
            gfx->print(playing ? "L:Pause" : "L:Play");
        }
        gfx->setCursor(52, 117);
        gfx->print("2R:Stop");
        gfx->setCursor(95, 117);
        gfx->print("R:Open");
    }

    void onOverlayLeft() {
        togglePlayPause();
    }

    void onOverlayRight(AppSwitchFn switchFn) {
        closeOverlay();
        if (_activeSource == AUDIO_SSYNC) {
            if (switchFn) switchFn(STATE_SSYNC);
        } else if (_activeSource == AUDIO_MUSIC) {
            if (switchFn) switchFn(STATE_MUSIC_UI);
        } else if (_activeSource == AUDIO_VIDEO) {
            if (switchFn) switchFn(STATE_VIDEO_UI);
        } else {
            if (switchFn) switchFn(STATE_SSYNC);
        }
    }
};

extern AudioManager* audioManager;
