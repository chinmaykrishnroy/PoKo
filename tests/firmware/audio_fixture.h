#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include "Arduino.h"
using SemaphoreHandle_t = std::recursive_mutex*;
constexpr int portMAX_DELAY = -1;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new std::recursive_mutex; }
void xSemaphoreTakeRecursive(SemaphoreHandle_t m, int) { m->lock(); }
void xSemaphoreGiveRecursive(SemaphoreHandle_t m) { m->unlock(); }
template<class T> T constrain(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }
struct Preferences {
    void begin(const char*, bool) {} void end() {} void putInt(const char*, int) {}
};
int getCurrentAppVolume() { return 50; }
void es8311Mute(bool) {} void setScaledVolume(int) {}
int outputReleased = 0;
void deinitI2S() { ++outputReleased; }
struct SnapPlayer {
    bool loaded = true, suspended = false, forceTimeout = false;
    std::atomic<bool> done{false};
    int waitTimeouts = 0;
    std::thread worker;
    void (*releaseFn)() = nullptr;
    bool isLoaded() { return loaded; }
    bool isSuspended() { return suspended; }
    void load(bool initial) { loaded = true; suspended = initial; }
    void setAudioCallbacks(bool (*)(), void (*release)(), void (*)(int, bool)) { releaseFn = release; }
    void suspendAudio() {
        suspended = true; done = false;
        if (worker.joinable()) worker.join();
        worker = std::thread([this] { releaseFn(); done = true; });
    }
    bool waitForSuspend(unsigned) {
        for (int i = 0; i < 100 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        bool ok = done && !forceTimeout;
        if (!ok) ++waitTimeouts;
        return ok;
    }
    void resumeAudio() { suspended = false; }
    bool waitForAudioReady(unsigned) { return true; }
    void stop() { suspendAudio(); }
    void unload() { waitForSuspend(200); loaded = false; }
    void setRemoteVolumePercent(int) {} void setMute(bool) {}
    ~SnapPlayer() { if (worker.joinable()) worker.join(); }
};

