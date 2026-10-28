#include <cstring>
#include <string>
#include "Arduino.h"
#include "RemotePlayback.h"
struct String : std::string {
    using std::string::string;
    String(const std::string& text):std::string(text) {}
    String(int n):std::string(std::to_string(n)) {}
};
size_t testStrlcpy(char* dst, const char* src, size_t len) { if (len) snprintf(dst,len,"%s",src); return strlen(src); }
#define strlcpy testStrlcpy
int serverStops = 0;
struct MediaServerRequest {
    void begin(String) {} void setConnectTimeout(int) {} void setTimeout(int) {}
    int GET() { ++serverStops; return 200; } void end() {}
};
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
    bool _cleanupPending = false, _paused = false, _dirty = false, _serverTransition = false;
    bool _streamStarted = true, _serverError = false, _serverOffline = false, _contentMissing = false;
    uint32_t _trackPos = 0, _streamRequestMs = 100, _lastSecondMs = 100, _playStartMs = 100;
    struct Song { char id[36]="old"; char title[44]="Old song"; char artist[28]="Old artist"; uint32_t duration_s = 300; } _songs[1];
    int _songCount=1,_pageStart=0,_catalogIndex=0,_catalogTotal=1,_scrollOffset=0;
    size_t _artSize=100;
    bool _artBitmapValid=true, listenerReady=true;
    char _loadedId[36]="old",_artRequestedId[36]="old";
    int _artFailures=0,artworkFetches=0;
    std::string artworkId;
    String getServerHost() { return "test"; } int getServerPort() { return 8765; }
    bool prepareRemoteStream() { return listenerReady; }
    bool prepareRemoteMedia(const RemotePlayback& media);
    void fetchArtwork(int n) { ++artworkFetches; artworkId=_songs[n].id; }
    int ended = 0;
    void requestStop();
    void onPlaybackEnded() { ++ended; }
    void stopPlayback();
    void stopAndReleaseMusic(bool releaseOwnership = true);
    void update();
};
