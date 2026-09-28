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
    static inline AudioManager* _instance = nullptr;
    AudioSource   _activeSource       = AUDIO_NONE;
    AudioSource   _suspendedSource    = AUDIO_NONE;
    bool          _transitioning      = false;
    int           _volume             = 80;
    bool          _volumeDirty        = false;
    uint32_t      _lastVolumeChangeMs = 0;
    SnapPlayer*   _snapPlayer         = nullptr;
    AudioActionFn _musicStopFn        = nullptr;
    AudioActionFn _musicToggleFn      = nullptr;
    AudioQueryFn  _musicIsPlayingFn   = nullptr;
    AudioStrFn    _musicGetTitleFn    = nullptr;
    AudioQueryFn  _musicErrorFn       = nullptr;
    AudioActionFn _videoStopFn        = nullptr;

    void deactivateSource(AudioSource src, bool suspend = false) {
        if (src == AUDIO_NONE) return;
        Serial.printf("[audioMgr] deactivating source %d (suspend=%d)\n", (int)src, (int)suspend);
        if (src == AUDIO_SSYNC) {
            if (_snapPlayer && _snapPlayer->isLoaded()) {
                if (suspend) {
                    _snapPlayer->suspendAudio();
                } else {
                    _snapPlayer->stop();
                    _snapPlayer->unload();
                }
            }
        } else if (src == AUDIO_MUSIC) {
            if (_musicStopFn) _musicStopFn();
        } else if (src == AUDIO_VIDEO) {
            if (_videoStopFn) _videoStopFn();
        }
    }

    bool activateSource(AudioSource src) {
        if (src == AUDIO_NONE) return true;
        Serial.printf("[audioMgr] activating source %d\n", (int)src);
        if (src == AUDIO_SSYNC) {
            if (_snapPlayer) {
                if (!_snapPlayer->isLoaded()) {
                    _snapPlayer->load(false);
                } else if (_snapPlayer->isSuspended()) {
                    _snapPlayer->resumeAudio();
                }
                return _snapPlayer->isLoaded();
            }
            return false;
        } else if (src == AUDIO_MUSIC) {
            return true;
        } else if (src == AUDIO_VIDEO) {
            return true;
        }
        return true;
    }

public:
    AudioManager() {
        _instance = this;
        _volume = getCurrentAppVolume();
    }

    static bool isSsyncActiveStatic() {
        return _instance ? _instance->_activeSource == AUDIO_SSYNC : true;
    }

    static void releaseOutputSsyncStatic() {
        if (_instance) _instance->releaseOutput(AUDIO_SSYNC);
    }

    static void onVolumeChangeStatic(int vol) {
        if (_instance) _instance->onSourceVolumeChanged(AUDIO_SSYNC, vol);
    }

    void onSourceVolumeChanged(AudioSource src, int vol) {
        if (_activeSource == src) {
            _volume = constrain(vol, 0, 100);
            setScaledVolume(_volume);
            _volumeDirty = true;
            _lastVolumeChangeMs = millis();
        }
    }

    void setSnapPlayer(SnapPlayer* p) {
        _snapPlayer = p;
        if (_snapPlayer) {
            _snapPlayer->setAudioCallbacks(isSsyncActiveStatic, releaseOutputSsyncStatic, onVolumeChangeStatic);
        }
    }
    void setMusicHandlers(AudioActionFn stopFn, AudioActionFn toggleFn, AudioQueryFn isPlayingFn, AudioStrFn getTitleFn = nullptr, AudioQueryFn errorFn = nullptr) {
        _musicStopFn = stopFn;
        _musicToggleFn = toggleFn;
        _musicIsPlayingFn = isPlayingFn;
        _musicGetTitleFn = getTitleFn;
        _musicErrorFn = errorFn;
    }
    void setVideoHandlers(AudioActionFn stopFn) {
        _videoStopFn = stopFn;
    }

    AudioSource activeSource() const { return _activeSource; }
    AudioSource suspendedSource() const { return _suspendedSource; }
    void setSuspendedSource(AudioSource src) { _suspendedSource = src; }

    bool request(AudioSource requested) {
        if (_transitioning) {
            Serial.println("[audioMgr] request rejected: transition in progress");
            return false;
        }
        _transitioning = true;

        if (requested == _activeSource) {
            if (requested == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isSuspended()) {
                _snapPlayer->resumeAudio();
            }
            _transitioning = false;
            return true;
        }

        if (requested == AUDIO_NONE) {
            stopAll();
            _transitioning = false;
            return true;
        }

        Serial.printf("[audioMgr] request transition %d -> %d (suspended=%d)\n",
                      (int)_activeSource, (int)requested, (int)_suspendedSource);

        AudioSource previous = _activeSource;

        if (previous != AUDIO_NONE) {
            bool canSuspend = (previous == AUDIO_SSYNC && (requested == AUDIO_MUSIC || requested == AUDIO_VIDEO));
            if (canSuspend) {
                deactivateSource(previous, true);
                _suspendedSource = AUDIO_SSYNC;
            } else {
                deactivateSource(previous, false);
                if (requested == AUDIO_SSYNC) {
                    _suspendedSource = AUDIO_NONE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(40));
        }

        if (!activateSource(requested)) {
            Serial.printf("[audioMgr] failed to activate %d, rolling back\n", (int)requested);
            if (_suspendedSource != AUDIO_NONE) {
                AudioSource susp = _suspendedSource;
                _suspendedSource = AUDIO_NONE;
                if (activateSource(susp)) {
                    _activeSource = susp;
                } else {
                    _activeSource = AUDIO_NONE;
                }
            } else {
                _activeSource = AUDIO_NONE;
            }
            _transitioning = false;
            return false;
        }

        _activeSource = requested;
        _transitioning = false;
        return true;
    }

    void release(AudioSource source) {
        if (_transitioning) {
            Serial.printf("[audioMgr] release ignored during active transition (%d)\n", (int)source);
            return;
        }
        if (_activeSource != source) return;

        Serial.printf("[audioMgr] release source %d (suspended=%d)\n", (int)source, (int)_suspendedSource);
        _activeSource = AUDIO_NONE;

        if (_suspendedSource != AUDIO_NONE) {
            AudioSource toRestore = _suspendedSource;
            _suspendedSource = AUDIO_NONE;
            Serial.printf("[audioMgr] restoring suspended source %d\n", (int)toRestore);
            if (activateSource(toRestore)) {
                _activeSource = toRestore;
            } else {
                _activeSource = AUDIO_NONE;
            }
        }
    }

    void releaseOutput(AudioSource src) {
        if (_activeSource == src || _activeSource == AUDIO_NONE) {
            if (_suspendedSource == AUDIO_NONE) {
                ::deinitI2S();
            }
        }
    }

    bool hasActiveSession() const {
        return (_activeSource != AUDIO_NONE) || (_snapPlayer && _snapPlayer->isLoaded());
    }

    void stopAll() {
        Serial.printf("[audioMgr] stopAll (active=%d, suspended=%d)\n", (int)_activeSource, (int)_suspendedSource);
        flushVolume();
        AudioSource cur = _activeSource;
        AudioSource susp = _suspendedSource;
        _activeSource = AUDIO_NONE;
        _suspendedSource = AUDIO_NONE;

        // 1. Stop active foreground producer first
        if (cur != AUDIO_NONE) {
            deactivateSource(cur, false);
        }
        // 2. Stop suspended background producer second
        if (susp != AUDIO_NONE && susp != cur) {
            deactivateSource(susp, false);
        }
    }

    void stopActiveSession() {
        stopAll();
    }

    void setVolume(int vol, bool persist = true) {
        int v = constrain(vol, 0, 100);
        _volume = v;
        setScaledVolume(v);

        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            _snapPlayer->setRemoteVolumePercent(v);
        }

        if (persist) {
            _volumeDirty = true;
            _lastVolumeChangeMs = millis();
        }
    }

    int getVolume() const {
        return _volume;
    }

    void rampVolume(int delta) {
        setVolume(_volume + delta, true);
    }

    void update() {
        if (_volumeDirty && (millis() - _lastVolumeChangeMs > 1500)) {
            _volumeDirty = false;
            Preferences p;
            p.begin("poko", false);
            p.putInt("volume", _volume);
            p.end();
            Serial.printf("[audioMgr] debounced volume saved: %d\n", _volume);
        }
    }

    void flushVolume() {
        if (_volumeDirty) {
            _volumeDirty = false;
            Preferences p;
            p.begin("poko", false);
            p.putInt("volume", _volume);
            p.end();
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
