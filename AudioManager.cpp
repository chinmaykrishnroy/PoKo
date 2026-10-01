#include "AudioManager.h"

AudioManager* AudioManager::_instance = nullptr;

AudioManager::MutexLock::MutexLock(SemaphoreHandle_t mutex) : m(mutex) {
    if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY);
}

AudioManager::MutexLock::~MutexLock() {
    if (m) xSemaphoreGiveRecursive(m);
}

bool AudioManager::deactivateSource(AudioSource src, bool suspend) {
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

bool AudioManager::activateSource(AudioSource src) {
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

AudioManager::AudioManager() {
        _instance = this;
        _mutex = xSemaphoreCreateRecursiveMutex();
        _volume = getCurrentAppVolume();
    }

bool AudioManager::isSsyncActiveStatic() {
        return _instance ? _instance->activeSource() == AUDIO_SSYNC : true;
    }

void AudioManager::releaseOutputSsyncStatic() {
        if (_instance) _instance->releaseOutput(AUDIO_SSYNC);
    }

void AudioManager::onVolumeChangeStatic(int vol, bool muted) {
        if (_instance) _instance->onSourceVolumeChanged(AUDIO_SSYNC, vol, muted);
    }

void AudioManager::onSourceVolumeChanged(AudioSource src, int vol, bool muted) {
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

void AudioManager::setSnapPlayer(SnapPlayer* p) {
        _snapPlayer = p;
        if (_snapPlayer) {
            _snapPlayer->setAudioCallbacks(isSsyncActiveStatic, releaseOutputSsyncStatic, onVolumeChangeStatic);
        }
    }

void AudioManager::setMusicHandlers(AudioActionFn stopFn, AudioActionFn toggleFn, AudioQueryFn isPlayingFn, AudioStrFn getTitleFn, AudioQueryFn errorFn) {
        _musicStopFn = stopFn;
        _musicToggleFn = toggleFn;
        _musicIsPlayingFn = isPlayingFn;
        _musicGetTitleFn = getTitleFn;
        _musicErrorFn = errorFn;
    }

void AudioManager::setVideoHandlers(AudioActionFn stopFn, AudioQueryFn stoppedFn) {
        _videoStopFn = stopFn;
        _videoStoppedFn = stoppedFn;
    }

AudioSource AudioManager::activeSource() const { MutexLock lock(_mutex); return _activeSource; }

AudioSource AudioManager::suspendedSource() const { MutexLock lock(_mutex); return _suspendedSource; }

void AudioManager::setSuspendedSource(AudioSource src) { MutexLock lock(_mutex); _suspendedSource = src; }

AudioSource AudioManager::getPhysicalOwner() const { MutexLock lock(_mutex); return _physicalOwner; }

void AudioManager::setPhysicalOwner(AudioSource src) { MutexLock lock(_mutex); _physicalOwner = src; }

bool AudioManager::request(AudioSource requested) {
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

void AudioManager::release(AudioSource source) {
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

void AudioManager::releaseOutput(AudioSource src) {
        if (src == AUDIO_NONE) return;
        MutexLock lock(_mutex);
        Serial.printf("[audioMgr] releaseOutput called by %d (owner=%d, active=%d, susp=%d)\n",
                      (int)src, (int)_physicalOwner, (int)_activeSource, (int)_suspendedSource);
        if (_physicalOwner == src || (_physicalOwner == AUDIO_NONE && _activeSource == src)) {
            ::deinitI2S();
            _physicalOwner = AUDIO_NONE;
        }
    }

bool AudioManager::hasActiveSession() const {
        MutexLock lock(_mutex);
        return (_activeSource != AUDIO_NONE) || (_snapPlayer && _snapPlayer->isLoaded());
    }

void AudioManager::stopAll() {
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

void AudioManager::stopActiveSession() {
        stopAll();
    }

void AudioManager::setVolume(int vol, bool persist) {
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

void AudioManager::setMute(bool muted) {
        MutexLock lock(_mutex);
        _isMuted = muted;
        es8311Mute(muted);
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            _snapPlayer->setMute(muted);
        }
    }

bool AudioManager::isMuted() const {
        MutexLock lock(_mutex);
        return _isMuted;
    }

int AudioManager::getVolume() const {
        MutexLock lock(_mutex);
        return _volume;
    }

void AudioManager::rampVolume(int delta) {
        setVolume(getVolume() + delta, true);
    }

void AudioManager::update() {
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

void AudioManager::flushVolume() {
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

bool AudioManager::isPlaying() const {
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

void AudioManager::togglePlayPause() {
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

const char* AudioManager::getSourceName() const {
        switch (_activeSource) {
            case AUDIO_SSYNC: return "SSync";
            case AUDIO_MUSIC: return "Music";
            case AUDIO_VIDEO: return "Video";
            case AUDIO_BLUETOOTH: return "Bluetooth";
            default: return "Idle";
        }
    }

bool AudioManager::hasError() const {
        if (_activeSource == AUDIO_SSYNC && _snapPlayer) {
            return _snapPlayer->isLoaded() && !_snapPlayer->isConnected();
        }
        if (_activeSource == AUDIO_MUSIC && _musicErrorFn) {
            return _musicErrorFn();
        }
        return false;
    }

bool AudioManager::isSoundPlaying() const {
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

bool AudioManager::isSessionActive() const {
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

uint16_t AudioManager::getSourceColor() const {
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

void AudioManager::drawStatusDot(Arduino_Canvas* canvas, int16_t x, int16_t y, int16_t r) {
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
