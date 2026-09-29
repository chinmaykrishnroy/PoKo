#include <string>
#include <cstring>
#include "Arduino.h"
#include "TitleMarquee.h"
struct String : std::string {
    using std::string::string;
    String(const std::string& s): std::string(s) {}
    String(int value): std::string(std::to_string(value)) {}
};
void delay(int) {}
bool ensureAudioOutput(int) { return true; }
enum { AUDIO_VIDEO, POWER_LOCK_DISPLAY=1, POWER_LOCK_REALTIME_NET=2, LOCK_OWNER_VIDEO };
struct AudioManager {
    int releases = 0;
    bool request(int) { return true; }
    void release(int) { ++releases; }
} audio;
auto* audioManager = &audio;
struct PowerManager {
    int locks = 3;
    void acquireLock(int bits, int) { locks |= bits; }
    void releaseLock(int bits, int) { locks &= ~bits; }
} power;
auto* powerManager = &power;
struct SyncedAVPlayer {
    bool loaded = true, failLoad = false, running = true, failUnload = false;
    int resetsWhileRunning = 0, unloads = 0, loads = 0;
    void reset() { if (running) ++resetsWhileRunning; }
    void unload() { ++unloads; if (!failUnload) running = loaded = false; }
    void load() { ++loads; running = loaded = !failLoad; }
    bool isLoaded() { return loaded; }
    bool isRunning() { return running; }
} player;
auto* syncPlugin = &player;
int httpResult = 202, requests = 0, listRequests = 0;
bool listResult = true;
struct HTTPClient {
    void begin(String) {} void setTimeout(int) {}
    int GET() { ++requests; return httpResult; }
    void end() {}
};
class VideoApp {
public:
    enum VideoMode { MODE_BROWSE, MODE_PLAYING };
    VideoMode _mode = MODE_PLAYING;
    struct Item { const char* id = "video"; unsigned duration_s = 10; } _videos[8];
    using VideoItem = Item;
    int _videoCount = 2, _selectedIdx = 0, _pageStart = 0, _catalogIndex = 0, _catalogTotal = 2, thumbnails = 0;
    unsigned _lastScrollMs = 0, _playStartMs = 0;
    bool _serverError = false, _dirty = false, _streamStarted = false;
    TitleMarquee _marquee;
    uint16_t _titleWidth = 0;
    String getServerHost() { return "test"; }
    int getServerPort() { return 8765; }
    void fetchThumbnail(int) { ++thumbnails; }
    bool fetchVideoList(int index, bool) {
        if (index >= _pageStart && index < _pageStart + _videoCount) {
            _catalogIndex = index;
            _selectedIdx = index - _pageStart;
            return true;
        }
        ++listRequests;
        if (!listResult) return false;
        _pageStart = (index / 8) * 8;
        _videoCount = std::min(8, _catalogTotal - _pageStart);
        _catalogIndex = index;
        _selectedIdx = index - _pageStart;
        _videos[_selectedIdx].id = "next";
        return true;
    }

