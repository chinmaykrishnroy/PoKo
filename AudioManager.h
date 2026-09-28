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

        if (_activeSource == source) {
            if (source == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isSuspended()) {
                _snapPlayer->resumeAudio();
            }
            return true;
        }

        Serial.printf("[audioMgr] request source %d (active=%d, suspended=%d)\n",
                      (int)source, (int)_activeSource, (int)_suspendedSource);

        if (source == AUDIO_SSYNC) {
            // Stop older music playback if active
            if (_activeSource == AUDIO_MUSIC && _musicStopFn) {
                _musicStopFn();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            _activeSource = AUDIO_SSYNC;
            _suspendedSource = AUDIO_NONE;
            if (_snapPlayer) {
                if (!_snapPlayer->isLoaded()) {
                    _snapPlayer->load();
                } else if (_snapPlayer->isSuspended()) {
                    _snapPlayer->resumeAudio();
                }
            }
            return true;
        }

        if (source == AUDIO_MUSIC) {
            // Suspend background SSync so it can be resumed when Music finishes
            if (_activeSource == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isLoaded()) {
                Serial.println("[audioMgr] suspending SSync for Music playback");
                _snapPlayer->suspendAudio();
                _suspendedSource = AUDIO_SSYNC;
            } else {
                _suspendedSource = AUDIO_NONE;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            _activeSource = AUDIO_MUSIC;
            return true;
        }

        if (source == AUDIO_VIDEO) {
            if (_activeSource == AUDIO_MUSIC && _musicStopFn) {
                _musicStopFn();
            }
            if (_activeSource == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isLoaded()) {
                Serial.println("[audioMgr] suspending SSync for Video playback");
                _snapPlayer->suspendAudio();
                _suspendedSource = AUDIO_SSYNC;
            } else {
                _suspendedSource = AUDIO_NONE;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            _activeSource = AUDIO_VIDEO;
            return true;
        }

        _activeSource = source;
        return true;
    }

    void release(AudioSource source) {
        if (_activeSource != source) return;

        Serial.printf("[audioMgr] release source %d (suspended=%d)\n", (int)source, (int)_suspendedSource);
        _activeSource = AUDIO_NONE;

        if (_suspendedSource == AUDIO_SSYNC) {
            Serial.println("[audioMgr] restoring suspended SSync audio session");
            _activeSource = AUDIO_SSYNC;
            _suspendedSource = AUDIO_NONE;
            if (_snapPlayer && _snapPlayer->isLoaded()) {
                _snapPlayer->resumeAudio();
            }
        } else {
            _suspendedSource = AUDIO_NONE;
        }
    }

    bool hasActiveSession() const {
        return (_activeSource != AUDIO_NONE) || (_snapPlayer && _snapPlayer->isLoaded());
    }

    void stopActiveSession() {
        Serial.printf("[audioMgr] stopActiveSession (active=%d, suspended=%d)\n",
                      (int)_activeSource, (int)_suspendedSource);
        AudioSource cur = _activeSource;
        AudioSource susp = _suspendedSource;
        _activeSource = AUDIO_NONE;
        _suspendedSource = AUDIO_NONE;
        if ((cur == AUDIO_MUSIC || susp == AUDIO_MUSIC || cur == AUDIO_NONE) && _musicStopFn) {
            _musicStopFn();
        }
        if ((cur == AUDIO_SSYNC || susp == AUDIO_SSYNC || cur == AUDIO_NONE) && _snapPlayer && _snapPlayer->isLoaded()) {
            _snapPlayer->stop();
            _snapPlayer->unload();
        }
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
};

extern AudioManager* audioManager;
