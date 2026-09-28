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
    AudioSource   _physicalOwner      = AUDIO_NONE;
    bool          _transitioning      = false;
    int           _volume             = 80;
    bool          _isMuted            = false;
    bool          _volumeDirty        = false;
    uint32_t      _lastVolumeChangeMs = 0;
    SemaphoreHandle_t _mutex          = nullptr;
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
                    if (!_snapPlayer->waitForSuspend(200)) {
                        Serial.println("[audioMgr] WARNING: suspend timed out waiting for snapAudio to release I2S");
                    }
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
                    _snapPlayer->load(true); // Always load suspended first
                    if (!_snapPlayer->isLoaded()) {
                        Serial.println("[audioMgr] snapPlayer failed to load suspended");
                        return false;
                    }
                }
                // Publish ownership BEFORE snapAudio attempts initI2S
                _activeSource = AUDIO_SSYNC;
                _snapPlayer->resumeAudio();
                bool ok = _snapPlayer->waitForAudioReady(1000);
                if (!ok) {
                    Serial.println("[audioMgr] SSync audio failed to ready");
                    _snapPlayer->suspendAudio();
                    _activeSource = AUDIO_NONE;
                    return false;
                }
                _physicalOwner = AUDIO_SSYNC;
                return true;
            }
            return false;
        } else if (src == AUDIO_MUSIC) {
            _activeSource = AUDIO_MUSIC;
            _physicalOwner = AUDIO_MUSIC;
            return true;
        } else if (src == AUDIO_VIDEO) {
            _activeSource = AUDIO_VIDEO;
            _physicalOwner = AUDIO_VIDEO;
            return true;
        }
        return true;
    }

public:
    AudioManager() {
        _instance = this;
        _mutex = xSemaphoreCreateMutex();
        _volume = getCurrentAppVolume();
    }

    static bool isSsyncActiveStatic() {
        return _instance ? _instance->_activeSource == AUDIO_SSYNC : true;
    }

    static void releaseOutputSsyncStatic() {
        if (_instance) _instance->releaseOutput(AUDIO_SSYNC);
    }

    static void onVolumeChangeStatic(int vol, bool muted) {
        if (_instance) _instance->onSourceVolumeChanged(AUDIO_SSYNC, vol, muted);
    }

    void onSourceVolumeChanged(AudioSource src, int vol, bool muted = false) {
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_activeSource == src) {
            _volume = constrain(vol, 0, 100);
            _isMuted = muted;
            es8311Mute(muted);
            setScaledVolume(_volume);
            _volumeDirty = true;
            _lastVolumeChangeMs = millis();
        }
        if (_mutex) xSemaphoreGive(_mutex);
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

    AudioSource getPhysicalOwner() const { return _physicalOwner; }
    void setPhysicalOwner(AudioSource src) { _physicalOwner = src; }

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
        }

        if (!activateSource(requested)) {
            Serial.printf("[audioMgr] failed to activate %d, rolling back\n", (int)requested);
            _activeSource = AUDIO_NONE;
            if (_suspendedSource != AUDIO_NONE) {
                AudioSource susp = _suspendedSource;
                _suspendedSource = AUDIO_NONE;
                if (!activateSource(susp)) {
                    _activeSource = AUDIO_NONE;
                }
            }
            _transitioning = false;
            return false;
        }

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
            if (!activateSource(toRestore)) {
                _activeSource = AUDIO_NONE;
                releaseOutput(source);
            }
        } else {
            releaseOutput(source);
        }
    }

    void releaseOutput(AudioSource src) {
        Serial.printf("[audioMgr] releaseOutput called by %d (owner=%d, active=%d, susp=%d)\n",
                      (int)src, (int)_physicalOwner, (int)_activeSource, (int)_suspendedSource);
        if (_physicalOwner == src || (_physicalOwner == AUDIO_NONE && _activeSource == src)) {
            ::deinitI2S();
            _physicalOwner = AUDIO_NONE;
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
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
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
        if (_mutex) xSemaphoreGive(_mutex);
    }

    void setMute(bool muted) {
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
        _isMuted = muted;
        es8311Mute(muted);
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            _snapPlayer->setMute(muted);
        }
        if (_mutex) xSemaphoreGive(_mutex);
    }

    bool isMuted() const {
        return _isMuted;
    }

    int getVolume() const {
        return _volume;
    }

    void rampVolume(int delta) {
        setVolume(_volume + delta, true);
    }

    void update() {
        if (_volumeDirty && (millis() - _lastVolumeChangeMs > 1500)) {
            if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
            if (_volumeDirty && (millis() - _lastVolumeChangeMs > 1500)) {
                _volumeDirty = false;
                int v = _volume;
                if (_mutex) xSemaphoreGive(_mutex);
                Preferences p;
                p.begin("poko", false);
                p.putInt("volume", v);
                p.end();
                Serial.printf("[audioMgr] debounced volume saved: %d\n", v);
                return;
            }
            if (_mutex) xSemaphoreGive(_mutex);
        }
    }

    void flushVolume() {
        if (_volumeDirty) {
            if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
            _volumeDirty = false;
            int v = _volume;
            if (_mutex) xSemaphoreGive(_mutex);
            Preferences p;
            p.begin("poko", false);
            p.putInt("volume", v);
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
        if (isDarkTheme()) {
            switch (_activeSource) {
                case AUDIO_SSYNC: return 0x07FF; // Cyan
                case AUDIO_MUSIC: return 0xF81F; // Pink
                case AUDIO_VIDEO: return 0x541F; // Blue
                default: return 0;
            }
        } else {
            // Light Theme: Deep high-contrast tones visible against light headers
            switch (_activeSource) {
                case AUDIO_SSYNC: return 0x0400; // Dark Forest Green / Deep Teal
                case AUDIO_MUSIC: return 0x90B0; // Deep Plum
                case AUDIO_VIDEO: return 0x0115; // Deep Navy
                default: return 0;
            }
        }
    }

    void drawStatusDot(Arduino_Canvas* canvas, int16_t x, int16_t y, int16_t r = 2) {
        if (!canvas || _activeSource == AUDIO_NONE) return;

        bool blinkPhase = ((millis() / 300) % 2 == 0);
        uint16_t color = getSourceColor();
        bool show = false;

        if (hasError()) {
            color = isDarkTheme() ? 0xF800 : 0xB000; // Red / Deep Crimson
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
