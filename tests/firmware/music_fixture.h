#include <cstring>
#include "Arduino.h"
uint32_t testMillis = 100;
int testPins[64];
int testPinSetups = 0;
enum { AUDIO_MUSIC };
struct Player {
    bool loaded = true, timeout = false;
    void stopStream() {}
    void unload() { if (!timeout) loaded = false; }
    bool isLoaded() { return loaded; }
    bool isConnected() { return false; }
    bool hasFinished() { return false; }
} player;
auto* audioPlugin = &player;
struct Manager { int releases = 0; void release(int) { ++releases; } } manager;
auto* audioManager = &manager;
struct Pixels { void setSongProgress(float) {} } pixelEngine;
class MusicApp {
public:
    enum { MODE_BROWSE, MODE_PLAYING };
    int _mode = MODE_PLAYING, _selectedIdx = 0;
    bool _cleanupPending = false, _paused = false, _dirty = false;
    bool _streamStarted = true, _serverError = false;
    uint32_t _trackPos = 0, _streamRequestMs = 100, _lastSecondMs = 100, _playStartMs = 100;
    struct Song { uint32_t duration_s = 300; } _songs[1];
    int ended = 0;
    void requestStop() {}
    void onPlaybackEnded() { ++ended; }
    void stopPlayback();
    void stopAndReleaseMusic(bool releaseOwnership = true);
    void update();
};
