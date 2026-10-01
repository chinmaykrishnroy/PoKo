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
        explicit MutexLock(SemaphoreHandle_t mutex);
        ~MutexLock();
    };

    static AudioManager* _instance;
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
    bool deactivateSource(AudioSource src, bool suspend = false);

    bool activateSource(AudioSource src);

public:
    AudioManager();

    static bool isSsyncActiveStatic();

    static void releaseOutputSsyncStatic();

    static void onVolumeChangeStatic(int vol, bool muted);

    void onSourceVolumeChanged(AudioSource src, int vol, bool muted = false);

    void setSnapPlayer(SnapPlayer* p);
    void setMusicHandlers(AudioActionFn stopFn, AudioActionFn toggleFn, AudioQueryFn isPlayingFn, AudioStrFn getTitleFn = nullptr, AudioQueryFn errorFn = nullptr);
    void setVideoHandlers(AudioActionFn stopFn, AudioQueryFn stoppedFn = nullptr);

    AudioSource activeSource() const;
    AudioSource suspendedSource() const;
    void setSuspendedSource(AudioSource src);

    AudioSource getPhysicalOwner() const;
    void setPhysicalOwner(AudioSource src);

    bool request(AudioSource requested);

    void release(AudioSource source);

    void releaseOutput(AudioSource src);

    bool hasActiveSession() const;

    void stopAll();

    void stopActiveSession();

    void setVolume(int vol, bool persist = true);

    void setMute(bool muted);

    bool isMuted() const;

    int getVolume() const;

    void rampVolume(int delta);

    void update();

    void flushVolume();

    bool isPlaying() const;

    void togglePlayPause();

    const char* getSourceName() const;

    bool hasError() const;

    bool isSoundPlaying() const;

    bool isSessionActive() const;

    uint16_t getSourceColor() const;

    void drawStatusDot(Arduino_Canvas* canvas, int16_t x, int16_t y, int16_t r = 2);
};

extern AudioManager* audioManager;

