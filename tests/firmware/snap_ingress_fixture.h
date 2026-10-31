#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../AudioActivity.h"
inline uint32_t testNow=1000;
inline uint32_t millis() { return testNow; }
struct SnapPlayer {
    struct Ring { bool full=true;size_t write(const uint8_t*,size_t n){return full?0:n;} } _pcmBuf;
    std::atomic<uint32_t> _resyncGeneration{0},_lastIncomingAudibleMs{0},_lastAudibleMs{0},_bytesDropped{0};
    std::atomic<bool> _producerAwaitingResync{false},_resyncRequested{false},_connected{true},_serverMuted{false};
    bool _decodeWriteFailed=false;
    void requestProducerResync(const char*) { _producerAwaitingResync=true;_resyncRequested=true; }
    bool writeRingExact(const uint8_t*,size_t);
    bool hasIncomingAudioSince(uint32_t) const;
};
