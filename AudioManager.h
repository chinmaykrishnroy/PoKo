#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
    struct MutexLock {
        SemaphoreHandle_t m;
        MutexLock(SemaphoreHandle_t mutex) : m(mutex) {
            if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY);
        }
        ~MutexLock() {
            if (m) xSemaphoreGiveRecursive(m);
        }
    };

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
    AudioQueryFn  _videoStoppedFn     = nullptr;

    // External producer operations may call back from another task. Never hold
    // _mutex while waiting for them; _transitioning serializes session changes.
    bool deactivateSource(AudioSource src, bool suspend = false) {
        if (src == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isLoaded()) {
            if (suspend) {
                _snapPlayer->suspendAudio();
                if (!_snapPlayer->waitForSuspend(1000)) {
                    Serial.println("[audioMgr] suspend timed out; refusing I2S handoff");
                    return false;
                }
            } else {
                _snapPlayer->stop();
                _snapPlayer->unload();
                if (_snapPlayer->isLoaded()) return false;
            }
        } else if (src == AUDIO_MUSIC) {
            if (_musicStopFn) _musicStopFn();
        } else if (src == AUDIO_VIDEO) {
            if (_videoStopFn) _videoStopFn();
            if (_videoStoppedFn && !_videoStoppedFn()) return false;
        }
        return true;
    }

    bool activateSource(AudioSource src) {
        if (src == AUDIO_SSYNC) {
            if (!_snapPlayer) return false;
            if (!_snapPlayer->isLoaded()) {
                _snapPlayer->load(true);
                if (!_snapPlayer->isLoaded()) return false;
            }
            {
                MutexLock lock(_mutex);
                // Publish before snapAudio's ownership query / initI2S.
                _activeSource = _physicalOwner = AUDIO_SSYNC;
            }
            _snapPlayer->resumeAudio();
            if (!_snapPlayer->waitForAudioReady(1000)) {
                _snapPlayer->suspendAudio();
                bool stopped = _snapPlayer->waitForSuspend(1000);
                MutexLock lock(_mutex);
                // Keep ownership attributed to SSync if its worker hasn't
                // acknowledged stopping. A later request must retry suspension.
                if (stopped) _activeSource = _physicalOwner = AUDIO_NONE;
                return false;
            }
            return true;
        }
        MutexLock lock(_mutex);
        _activeSource = _physicalOwner = src;
        return true;
    }

public:
    AudioManager() {
        _instance = this;
        _mutex = xSemaphoreCreateRecursiveMutex();
        _volume = getCurrentAppVolume();
    }

    static bool isSsyncActiveStatic() {
        return _instance ? _instance->activeSource() == AUDIO_SSYNC : true;
    }

    static void releaseOutputSsyncStatic() {
        if (_instance) _instance->releaseOutput(AUDIO_SSYNC);
    }

    static void onVolumeChangeStatic(int vol, bool muted) {
        if (_instance) _instance->onSourceVolumeChanged(AUDIO_SSYNC, vol, muted);
    }

    void onSourceVolumeChanged(AudioSource src, int vol, bool muted = false) {
        MutexLock lock(_mutex);
        if (_activeSource == src) {
            _volume = constrain(vol, 0, 100);
            _isMuted = muted;
            es8311Mute(muted);
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
    void setVideoHandlers(AudioActionFn stopFn, AudioQueryFn stoppedFn = nullptr) {
        _videoStopFn = stopFn;
        _videoStoppedFn = stoppedFn;
    }

    AudioSource activeSource() const { MutexLock lock(_mutex); return _activeSource; }
    AudioSource suspendedSource() const { MutexLock lock(_mutex); return _suspendedSource; }
    void setSuspendedSource(AudioSource src) { MutexLock lock(_mutex); _suspendedSource = src; }

    AudioSource getPhysicalOwner() const { MutexLock lock(_mutex); return _physicalOwner; }
    void setPhysicalOwner(AudioSource src) { MutexLock lock(_mutex); _physicalOwner = src; }

    bool request(AudioSource requested) {
        if (requested == AUDIO_NONE) {
            stopAll();
            MutexLock lock(_mutex);
            return !_transitioning && _activeSource == AUDIO_NONE && _physicalOwner == AUDIO_NONE;
        }
        AudioSource previous;
        {
            MutexLock lock(_mutex);
            if (_transitioning) return false;
            previous = _activeSource;
            if (requested == previous &&
                !(requested == AUDIO_SSYNC && _snapPlayer && _snapPlayer->isSuspended())) return true;
            _transitioning = true;
        }
        bool canSuspend = previous == AUDIO_SSYNC &&
                          (requested == AUDIO_MUSIC || requested == AUDIO_VIDEO);
        if (previous != AUDIO_NONE && previous != requested) {
            if (!deactivateSource(previous, canSuspend)) {
                MutexLock lock(_mutex);
                _transitioning = false;
                return false;
            }
            releaseOutput(previous);
            MutexLock lock(_mutex);
            _activeSource = AUDIO_NONE;
            if (canSuspend) _suspendedSource = AUDIO_SSYNC;
            else if (requested == AUDIO_SSYNC) _suspendedSource = AUDIO_NONE;
        }
        bool ok = activateSource(requested);
        if (!ok) {
            AudioSource restore;
            {
                MutexLock lock(_mutex);
                restore = _suspendedSource;
                _suspendedSource = AUDIO_NONE;
            }
            if (restore != AUDIO_NONE) activateSource(restore);
        }
        {
            MutexLock lock(_mutex);
            _transitioning = false;
        }
        return ok;
    }

    void release(AudioSource source) {
        AudioSource restore;
        {
            MutexLock lock(_mutex);
            if (_transitioning || _activeSource != source) return;
            _transitioning = true;
            restore = _suspendedSource;
            _suspendedSource = AUDIO_NONE;
            _activeSource = AUDIO_NONE;
        }
        // The foreground producer has stopped. Release its output before
        // letting the resumed Snap worker reconfigure the shared I2S channel.
        releaseOutput(source);
        if (restore != AUDIO_NONE) activateSource(restore);
        MutexLock lock(_mutex);
        _transitioning = false;
    }

    void releaseOutput(AudioSource src) {
        if (src == AUDIO_NONE) return;
        MutexLock lock(_mutex);
        Serial.printf("[audioMgr] releaseOutput called by %d (owner=%d, active=%d, susp=%d)\n",
                      (int)src, (int)_physicalOwner, (int)_activeSource, (int)_suspendedSource);
        if (_physicalOwner == src || (_physicalOwner == AUDIO_NONE && _activeSource == src)) {
            ::deinitI2S();
            _physicalOwner = AUDIO_NONE;
        }
    }

    bool hasActiveSession() const {
        MutexLock lock(_mutex);
        return (_activeSource != AUDIO_NONE) || (_snapPlayer && _snapPlayer->isLoaded());
    }

    void stopAll() {
        AudioSource current, suspended;
        {
            MutexLock lock(_mutex);
            if (_transitioning) return;
            _transitioning = true;
            current = _activeSource;
            suspended = _suspendedSource;
        }
        flushVolume();
        bool stopped = deactivateSource(current, false);
        if (stopped) releaseOutput(current);
        bool backgroundStopped = suspended == AUDIO_NONE || suspended == current ||
                                 deactivateSource(suspended, false);
        if (backgroundStopped && suspended != AUDIO_NONE) releaseOutput(suspended);
        MutexLock lock(_mutex);
        if (stopped) _activeSource = AUDIO_NONE;
        if (backgroundStopped) _suspendedSource = AUDIO_NONE;
        _transitioning = false;
    }

    void stopActiveSession() {
        stopAll();
    }

    void setVolume(int vol, bool persist = true) {
        MutexLock lock(_mutex);
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

    void setMute(bool muted) {
        MutexLock lock(_mutex);
        _isMuted = muted;
        es8311Mute(muted);
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            _snapPlayer->setMute(muted);
        }
    }

    bool isMuted() const {
        MutexLock lock(_mutex);
        return _isMuted;
    }

    int getVolume() const {
        MutexLock lock(_mutex);
        return _volume;
    }

    void rampVolume(int delta) {
        setVolume(getVolume() + delta, true);
    }

    void update() {
        int v = -1;
        {
            MutexLock lock(_mutex);
            if (_volumeDirty && (millis() - _lastVolumeChangeMs > 1500)) {
                _volumeDirty = false;
                v = _volume;
            }
        }
        if (v >= 0) {
            Preferences p;
            p.begin("poko", false);
            p.putInt("volume", v);
            p.end();
            Serial.printf("[audioMgr] debounced volume saved: %d\n", v);
        }
    }

    void flushVolume() {
        int v = -1;
        {
            MutexLock lock(_mutex);
            if (_volumeDirty) {
                _volumeDirty = false;
                v = _volume;
            }
        }
        if (v >= 0) {
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

