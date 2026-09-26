#pragma once
#include <WiFi.h>
#include <Arduino_GFX_Library.h>
#include <driver/i2s_std.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>
#include "PokoPins.h"
#include "PokoDrivers.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <ESP8266Audio.h>

extern "C" {
#include "libflac/FLAC/stream_decoder.h"
#include "libopus/include/opus.h"
}

extern String getNetworkStatusMsg();

// Lock-free Single-Producer / Single-Consumer ring buffer.
// snapNet task is the SOLE writer (updates _head).
// snapAudio task is the SOLE reader (updates _tail).
// No mutex needed — volatile + memory fences give correct ordering on Xtensa SMP.
class SnapAudioRingBuffer {
private:
    uint8_t* _buffer;
    size_t _capacity;
    volatile size_t _head;  // written only by producer
    volatile size_t _tail;  // written only by consumer

public:
    SnapAudioRingBuffer() : _buffer(nullptr), _capacity(0), _head(0), _tail(0) {}

    ~SnapAudioRingBuffer() {
        freeBuffer();
    }

    bool init(size_t size) {
        freeBuffer();
        size &= ~3; // Align to 4-byte frame boundary
        if (psramFound()) {
            _buffer = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!_buffer) {
            _buffer = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!_buffer) return false;
        _capacity = size;
        _head = 0;
        _tail = 0;
        return true;
    }

    void freeBuffer() {
        if (_buffer) {
            heap_caps_free(_buffer);
            _buffer = nullptr;
        }
        _capacity = 0;
        _head = 0;
        _tail = 0;
    }

    // Reset — call only when both producer and consumer tasks are stopped.
    void reset() {
        _head = 0;
        _tail = 0;
    }

    // Thread-safe drain for consumer task — discards buffered audio without racing producer
    void drain() {
        size_t h = _head;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        _tail = h;
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    bool isAllocated() const {
        return _buffer != nullptr;
    }

    size_t capacity() const {
        return _capacity;
    }

    // Bytes available to read (always 4-byte frame aligned).
    size_t available() const {
        size_t h = _head;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        size_t t = _tail;
        size_t rawAvail = (h >= t) ? (h - t) : (_capacity - (t - h));
        return rawAvail & ~3;
    }

    // Free bytes available to write (always 4-byte frame aligned, leaving 4-byte gap).
    size_t freeSpace() const {
        if (_capacity == 0) return 0;
        size_t t = _tail;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        size_t h = _head;
        size_t used = (h >= t) ? (h - t) : (_capacity - (t - h));
        if (_capacity <= used + 4) return 0;
        return (_capacity - used - 4) & ~3;
    }

    // Write up to len bytes. Guaranteed strictly 4-byte aligned to prevent sample phase corruption.
    size_t write(const uint8_t* data, size_t len) {
        if (!_buffer || len == 0 || _capacity == 0) return 0;
        size_t toWrite = min(len, freeSpace()) & ~3;
        if (toWrite == 0) return 0;

        size_t h = _head;
        size_t firstChunk = min(toWrite, _capacity - h);
        memcpy(_buffer + h, data, firstChunk);
        if (toWrite > firstChunk) {
            memcpy(_buffer, data + firstChunk, toWrite - firstChunk);
        }

        __atomic_thread_fence(__ATOMIC_RELEASE);
        _head = (h + toWrite) % _capacity;
        return toWrite;
    }

    // Read up to len bytes into dest. Guaranteed strictly 4-byte aligned.
    size_t read(uint8_t* dest, size_t len) {
        if (!_buffer || len == 0 || _capacity == 0) return 0;
        size_t toRead = min(len, available()) & ~3;
        if (toRead == 0) return 0;

        size_t t = _tail;
        size_t firstChunk = min(toRead, _capacity - t);
        memcpy(dest, _buffer + t, firstChunk);
        if (toRead > firstChunk) {
            memcpy(dest + firstChunk, _buffer, toRead - firstChunk);
        }

        __atomic_thread_fence(__ATOMIC_RELEASE);
        _tail = (t + toRead) % _capacity;
        return toRead;
    }
};

class SnapPlayer {
private:
    static const size_t RING_BUFFER_SIZE = 786432; // 768 KB (~4.1 seconds of 48kHz 16-bit stereo)
    static const uint16_t TIME_SYNC_INTERVAL_MS = 1000;

    struct SnapBaseHeader {
        uint16_t type;
        uint16_t id;
        uint16_t refersTo;
        int32_t sent_sec;
        int32_t sent_usec;
        int32_t recv_sec;
        int32_t recv_usec;
        uint32_t size;
    };

    enum SnapMsgType {
        SNAP_MSG_BASE = 0,
        SNAP_MSG_CODEC_HEADER = 1,
        SNAP_MSG_WIRE_CHUNK = 2,
        SNAP_MSG_SERVER_SETTINGS = 3,
        SNAP_MSG_TIME = 4,
        SNAP_MSG_HELLO = 5,
        SNAP_MSG_CLIENT_INFO = 7
    };

    void* _tft = nullptr;
    Preferences* _prefs;
    String _serverHost;
    uint16_t _serverPort;
    int32_t _customLatencyMs;
    volatile float _volume;

    volatile bool _isRunning;
    bool _isLoaded;
    volatile bool _connected;
    volatile bool _syncing;
    volatile bool _playStarted;
    volatile bool _playReleased;

    TaskHandle_t _netTaskHandle;
    TaskHandle_t _audioTaskHandle;
    SemaphoreHandle_t _netTaskDone;
    SemaphoreHandle_t _audioTaskDone;
    bool _netTaskStarted;
    bool _audioTaskStarted;
    bool _i2sInstalled;
    volatile bool _resyncRequested;
    volatile bool _volumePublishPending;
    bool _receivedInitialServerSettings;
    bool _audioFault;
    SnapAudioRingBuffer _pcmBuf;
    WiFiClient _client;

    enum KnobMode { KNOB_VOLUME, KNOB_LATENCY };
    KnobMode _knobMode;

    String _codec;
    uint32_t _sampleRate;
    uint16_t _channels;
    uint16_t _bitsPerSample;
    int32_t _serverBufferMs;
    int32_t _serverLatencyMs;
    volatile int32_t _measuredLatencyMs;
    int32_t _serverVolume;
    bool _serverMuted;

    static const int DMA_BUF_COUNT = 6;
    static const int DMA_BUF_LEN = 128;
    static const int DMA_TOTAL_FRAMES = DMA_BUF_COUNT * DMA_BUF_LEN; // 768 frames (~16ms @ 48kHz)

    // Clock/sync filtering. 31 clock samples rejects Wi-Fi jitter while the short
    // 9-sample playback-error median keeps the PLL from reacting to one noisy block.
    static const size_t TIME_DIFF_FILTER_SIZE = 31;
    static const size_t AGE_FILTER_SIZE = 9;
    static const size_t TIME_REQUEST_SLOTS = 16;
    static constexpr int64_t CHUNK_TS_TOLERANCE_US = 5000;
    static constexpr uint8_t EMPTY_READS_BEFORE_RESYNC = 4;
    static constexpr uint32_t MAX_WIRE_CHUNK_BYTES = 256U * 1024U;
    static constexpr uint32_t MAX_CODEC_HEADER_BYTES = 64U * 1024U;
    static constexpr uint32_t MAX_SETTINGS_JSON_BYTES = 4096U;

    // Start-of-stream pop suppression. Priming stabilizes BCLK/WS/DOUT before
    // program audio is released; the fade changes amplitude only, never timing.
    static constexpr uint32_t I2S_PRIME_MS = 60U;
    static constexpr uint32_t STARTUP_FADE_MS = 30U;

    struct TimeRequestStamp {
        uint16_t id;
        int64_t sentUs;
        bool valid;
    };

    int64_t _diffHistory[TIME_DIFF_FILTER_SIZE];
    size_t _diffCount;
    size_t _diffIdx;
    int64_t _ageHistory[AGE_FILTER_SIZE];
    size_t _ageCount;
    size_t _ageIdx;
    TimeRequestStamp _timeRequests[TIME_REQUEST_SLOTS];

    double _correctionAccumulator;
    double _pllIntegralPpm;
    double _lastCorrectionPpm;

    int64_t _diffToServerUs;
    uint32_t _lastTimeSyncMs;
    int64_t _lastTimeSentUs;
    uint16_t _timeMsgId;

    int64_t _firstChunkServerTsUs;
    int64_t _targetPlayLocalTimeUs;
    int64_t _expectedNextChunkTsUs;
    uint64_t _samplesPlayed;
    uint64_t _decodedFramesThisChunk;
    uint32_t _chunksReceived;
    uint32_t _bytesDropped;
    uint32_t _underruns;
    uint32_t _i2sShortWrites;
    uint32_t _chunkTimestampResyncs;
    uint32_t _timeSyncRejects;
    uint32_t _timeSyncUnmatched;
    uint8_t _consecutiveEmptyReads;
    int32_t _lastDriftMs;
    int32_t _lastDriftUs;
    volatile bool _producerAwaitingResync;
    volatile bool _decodeWriteFailed;
    volatile uint32_t _resyncGeneration;
    uint32_t _fadeFramesTotal;
    uint32_t _fadeFramesDone;

    uint32_t _lastOverlayUpdateMs;
    uint32_t _timeSyncCount;
    bool _receivedCodecHeader;

    inline int32_t getEffectiveBufferMs() const {
        int32_t eff = _serverBufferMs - _serverLatencyMs - _customLatencyMs;
        return max((int32_t)0, eff);
    }

    void addTimeDiffSample(int64_t diff) {
        _diffHistory[_diffIdx] = diff;
        _diffIdx = (_diffIdx + 1) % TIME_DIFF_FILTER_SIZE;
        if (_diffCount < TIME_DIFF_FILTER_SIZE) _diffCount++;

        int64_t temp[TIME_DIFF_FILTER_SIZE];
        memcpy(temp, _diffHistory, _diffCount * sizeof(int64_t));
        for (size_t i = 1; i < _diffCount; i++) {
            int64_t key = temp[i];
            int j = i - 1;
            while (j >= 0 && temp[j] > key) {
                temp[j + 1] = temp[j];
                j--;
            }
            temp[j + 1] = key;
        }
        _diffToServerUs = temp[_diffCount / 2];
    }

    void resetTimeSyncState() {
        _diffToServerUs = 0;
        _diffCount = 0;
        _diffIdx = 0;
        _timeSyncCount = 0;
        _lastTimeSentUs = 0;
        memset(_diffHistory, 0, sizeof(_diffHistory));
        memset(_timeRequests, 0, sizeof(_timeRequests));
    }

    void resetPllState() {
        _correctionAccumulator = 0.0;
        _pllIntegralPpm = 0.0;
        _lastCorrectionPpm = 0.0;
        _ageCount = 0;
        _ageIdx = 0;
        _consecutiveEmptyReads = 0;
        _lastDriftMs = 0;
        _lastDriftUs = 0;
        memset(_ageHistory, 0, sizeof(_ageHistory));
    }

    int64_t addAgeSample(int64_t ageUs) {
        _ageHistory[_ageIdx] = ageUs;
        _ageIdx = (_ageIdx + 1) % AGE_FILTER_SIZE;
        if (_ageCount < AGE_FILTER_SIZE) _ageCount++;

        int64_t temp[AGE_FILTER_SIZE];
        memcpy(temp, _ageHistory, _ageCount * sizeof(int64_t));
        for (size_t i = 1; i < _ageCount; i++) {
            int64_t key = temp[i];
            int j = (int)i - 1;
            while (j >= 0 && temp[j] > key) {
                temp[j + 1] = temp[j];
                j--;
            }
            temp[j + 1] = key;
        }
        return temp[_ageCount / 2];
    }

    void rememberTimeRequest(uint16_t id, int64_t sentUs) {
        size_t slot = id % TIME_REQUEST_SLOTS;
        _timeRequests[slot].id = id;
        _timeRequests[slot].sentUs = sentUs;
        _timeRequests[slot].valid = true;
    }

    bool takeTimeRequest(uint16_t id, int64_t& sentUs) {
        size_t slot = id % TIME_REQUEST_SLOTS;
        TimeRequestStamp& req = _timeRequests[slot];
        if (!req.valid || req.id != id) return false;
        sentUs = req.sentUs;
        req.valid = false;
        return true;
    }

    void requestProducerResync(const char* reason) {
        if (!_producerAwaitingResync) {
            Serial.printf("[snap] Producer resync requested: %s\n", reason ? reason : "unknown");
        }
        _producerAwaitingResync = true;
        _resyncRequested = true;
        _expectedNextChunkTsUs = 0;
    }

    bool writeRingExact(const uint8_t* data, size_t bytes) {
        if (bytes == 0) return true;
        uint32_t generation = _resyncGeneration;
        if (_producerAwaitingResync) {
            _decodeWriteFailed = true;
            return false;
        }

        size_t sent = _pcmBuf.write(data, bytes);

        // If snapAudio resynced while this copy was in progress, these bytes may
        // have landed after its drain. Request one more drain so stale PCM can
        // never survive across a timeline generation boundary.
        if (generation != _resyncGeneration || _producerAwaitingResync) {
            _decodeWriteFailed = true;
            _resyncRequested = true;
            return false;
        }

        if (sent == bytes) return true;

        _bytesDropped += (uint32_t)(bytes - sent);
        _decodeWriteFailed = true;
        requestProducerResync("PCM ring full / partial write");
        return false;
    }

    bool ensureEncodedScratch(size_t bytes) {
        if (bytes == 0) return false;
        if (_encodedChunkBuf && _encodedChunkCap >= bytes) return true;

        if (_encodedChunkBuf) {
            heap_caps_free(_encodedChunkBuf);
            _encodedChunkBuf = nullptr;
            _encodedChunkCap = 0;
        }

        size_t target = max((size_t)8192, bytes);
        if (psramFound()) {
            _encodedChunkBuf = (uint8_t*)heap_caps_malloc(target, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!_encodedChunkBuf) {
            _encodedChunkBuf = (uint8_t*)heap_caps_malloc(target, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!_encodedChunkBuf) return false;

        _encodedChunkCap = target;
        return true;
    }

    void cleanupEncodedScratch() {
        if (_encodedChunkBuf) {
            heap_caps_free(_encodedChunkBuf);
            _encodedChunkBuf = nullptr;
        }
        _encodedChunkCap = 0;
    }

    // FLAC Decoder state
    FLAC__StreamDecoder* _flacDecoder;
    const uint8_t* _flacInputPtr;
    size_t _flacInputRemaining;

    // Opus Decoder state
    OpusDecoder* _opusDecoder;
    int16_t* _opusPcmBuf;
    uint8_t* _opusEncodedBuf;

    // Reusable encoded-chunk scratch buffer. Reusing one PSRAM allocation avoids
    // malloc/free churn on every FLAC/large Opus packet during multi-day playback.
    uint8_t* _encodedChunkBuf;
    size_t _encodedChunkCap;

    bool _overlayStaticDrawn;

    // TFT overlay cache. Snapclient updates several counters frequently; repainting
    // the whole information panel every 500 ms causes visible blinking on ST7789.
    // Each field below is redrawn only when its rendered value actually changes.
    String _uiBadgeKey;
    String _uiServerKey;
    String _uiCodecKey;
    String _uiSyncKey;
    String _uiDriftKey;
    String _uiClockKey;
    String _uiVolumeKey;
    String _uiFooterKey;
    int _uiVolumeFillW;
    bool _uiVolumeMuted;

    void resetOverlayCache() {
        _uiBadgeKey = "";
        _uiServerKey = "";
        _uiCodecKey = "";
        _uiSyncKey = "";
        _uiDriftKey = "";
        _uiClockKey = "";
        _uiVolumeKey = "";
        _uiFooterKey = "";
        _uiVolumeFillW = -1;
        _uiVolumeMuted = false;
    }

    String fitUiText(const String& text, int maxWidth, uint8_t font) {
        return text;
    }

    void drawCachedUiLine(String& cacheKey, const String& text, int x, int y,
                          int width, uint16_t color, uint8_t font = 2) {
    }

    static inline uint16_t readU16LE(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    static inline uint32_t readU32LE(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    static inline int32_t readI32LE(const uint8_t* p) {
        return (int32_t)readU32LE(p);
    }

    static inline void writeU16LE(uint8_t* p, uint16_t v) {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
    }
    static inline void writeU32LE(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
        p[2] = (uint8_t)((v >> 16) & 0xFF);
        p[3] = (uint8_t)((v >> 24) & 0xFF);
    }
    static inline void writeI32LE(uint8_t* p, int32_t v) {
        writeU32LE(p, (uint32_t)v);
    }

    static String u64String(uint64_t value) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
        return String(buf);
    }

    void muteI2SPins() {
        es8311Mute(true);
    }

    bool primeI2SPath(uint32_t sampleRate) {
        if (!_i2sInstalled || sampleRate == 0 || !poko_tx_handle) return false;
        int16_t silence[DMA_BUF_LEN * 2] = {0};
        uint32_t framesRemaining = max((uint32_t)DMA_BUF_LEN,
            (uint32_t)(((uint64_t)sampleRate * I2S_PRIME_MS) / 1000ULL));

        while (framesRemaining > 0 && _isRunning) {
            uint32_t frames = min((uint32_t)DMA_BUF_LEN, framesRemaining);
            size_t bytes = (size_t)frames * 2U * sizeof(int16_t);
            size_t written = 0;
            esp_err_t err = i2s_channel_write(poko_tx_handle, silence, bytes, &written, pdMS_TO_TICKS(50));
            if (err != ESP_OK || written != bytes) {
                return false;
            }
            framesRemaining -= frames;
        }
        return true;
    }

    void armStartupFade(uint32_t sampleRate) {
        _fadeFramesDone = 0;
        _fadeFramesTotal = max((uint32_t)1,
            (uint32_t)(((uint64_t)sampleRate * STARTUP_FADE_MS) / 1000ULL));
    }

    bool initI2S(uint32_t sampleRate) {
        if (!ensureAudioOutput(sampleRate)) {
            Serial.printf("[snap] ensureAudioOutput failed: %lu Hz\n", (unsigned long)sampleRate);
            _i2sInstalled = false;
            return false;
        }

        setScaledVolume(_serverMuted ? 0 : _serverVolume);
        _i2sInstalled = true;
        primeI2SPath(sampleRate);
        return true;
    }

    void deinitI2S() {
        if (!_i2sInstalled) return;
        ::deinitI2S();
        _i2sInstalled = false;
    }

    bool readExact(WiFiClient& client, uint8_t* dest, size_t len, uint32_t timeoutMs = 3000) {
        size_t readBytes = 0;
        uint32_t startWait = millis();
        while (readBytes < len && _isRunning && client.connected()) {
            int avail = client.available();
            if (avail > 0) {
                int toRead = min((size_t)avail, len - readBytes);
                int r = client.read(dest + readBytes, toRead);
                if (r > 0) {
                    readBytes += r;
                    startWait = millis();
                }
            } else {
                if (millis() - startWait > timeoutMs) return false;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        return readBytes == len;
    }

    bool discardBytes(WiFiClient& client, size_t len, uint32_t timeoutMs = 3000) {
        uint8_t dummy[256];
        while (len > 0 && _isRunning && client.connected()) {
            size_t chunk = min(sizeof(dummy), len);
            if (!readExact(client, dummy, chunk, timeoutMs)) return false;
            len -= chunk;
        }
        return len == 0;
    }

    int16_t decodePcmSample(const uint8_t* p, uint16_t bits) const {
        if (bits == 8) {
            return (int16_t)((int16_t)(int8_t)p[0] << 8);
        }
        if (bits == 16) {
            return (int16_t)readU16LE(p);
        }
        if (bits == 24) {
            int32_t v = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
            if (v & 0x00800000) v |= 0xFF000000;
            return (int16_t)(v >> 8);
        }
        if (bits == 32) {
            int32_t v = (int32_t)readU32LE(p);
            return (int16_t)(v >> 16);
        }
        return 0;
    }

    bool writeDecodedPcm(const int16_t* pcm, uint32_t frames, uint16_t channels) {
        if (!pcm || frames == 0) return true;
        if (channels == 2) {
            return writeRingExact((const uint8_t*)pcm, (size_t)frames * 2U * sizeof(int16_t));
        }
        if (channels != 1) return false;

        int16_t stereo[256 * 2];
        uint32_t pos = 0;
        while (pos < frames) {
            uint32_t n = min((uint32_t)256, frames - pos);
            for (uint32_t i = 0; i < n; ++i) {
                int16_t v = pcm[pos + i];
                stereo[i * 2] = v;
                stereo[i * 2 + 1] = v;
            }
            if (!writeRingExact((const uint8_t*)stereo, (size_t)n * 2U * sizeof(int16_t))) return false;
            pos += n;
        }
        return true;
    }

    bool processPcmPayload(WiFiClient& client, uint32_t payloadBytes, uint64_t& framesOut) {
        framesOut = 0;
        if (!ensureEncodedScratch(payloadBytes)) {
            discardBytes(client, payloadBytes);
            requestProducerResync("PCM scratch allocation failed");
            return false;
        }
        if (!readExact(client, _encodedChunkBuf, payloadBytes, 3000)) return false;

        uint16_t channels = _channels;
        uint16_t bits = _bitsPerSample;
        if ((channels != 1 && channels != 2) ||
            (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
            Serial.printf("[snap] Unsupported PCM format: %u ch / %u bit\n", channels, bits);
            requestProducerResync("unsupported PCM format");
            return false;
        }

        const size_t bytesPerSample = bits / 8U;
        const size_t bytesPerFrame = bytesPerSample * channels;
        if (bytesPerFrame == 0 || (payloadBytes % bytesPerFrame) != 0) {
            requestProducerResync("misaligned PCM payload");
            return false;
        }

        uint32_t totalFrames = payloadBytes / bytesPerFrame;
        int16_t stereo[256 * 2];
        uint32_t pos = 0;
        while (pos < totalFrames) {
            uint32_t n = min((uint32_t)256, totalFrames - pos);
            for (uint32_t i = 0; i < n; ++i) {
                const uint8_t* frame = _encodedChunkBuf + (size_t)(pos + i) * bytesPerFrame;
                int16_t left = decodePcmSample(frame, bits);
                int16_t right = channels > 1 ? decodePcmSample(frame + bytesPerSample, bits) : left;
                stereo[i * 2] = left;
                stereo[i * 2 + 1] = right;
            }
            if (!writeRingExact((const uint8_t*)stereo, (size_t)n * 2U * sizeof(int16_t))) return false;
            pos += n;
        }

        framesOut = totalFrames;
        return true;
    }

    bool writeAll(WiFiClient& client, const uint8_t* data, size_t len, uint32_t timeoutMs = 2000) {
        size_t sent = 0;
        uint32_t lastProgress = millis();

        while (sent < len && _isRunning && client.connected()) {
            size_t n = client.write(data + sent, len - sent);
            if (n > 0) {
                sent += n;
                lastProgress = millis();
                continue;
            }

            if (millis() - lastProgress >= timeoutMs) return false;
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        return sent == len;
    }

    bool sendBaseMessage(WiFiClient& client, uint16_t type, uint16_t id, uint16_t refersTo,
                         int64_t sentUs, uint32_t payloadSize, const uint8_t* payload = nullptr) {
        uint8_t hdr[26];
        writeU16LE(hdr + 0, type);
        writeU16LE(hdr + 2, id);
        writeU16LE(hdr + 4, refersTo);
        writeI32LE(hdr + 6, (int32_t)(sentUs / 1000000LL));
        writeI32LE(hdr + 10, (int32_t)(sentUs % 1000000LL));
        writeI32LE(hdr + 14, 0);
        writeI32LE(hdr + 18, 0);
        writeU32LE(hdr + 22, payloadSize);

        if (!writeAll(client, hdr, sizeof(hdr))) return false;
        if (payloadSize > 0 && payload != nullptr) {
            if (!writeAll(client, payload, payloadSize)) return false;
        }
        return true;
    }

    bool sendHello(WiFiClient& client) {
        String mac = WiFi.macAddress();
        StaticJsonDocument<384> doc;
        doc["Arch"] = "esp32s3";
        doc["ClientName"] = "PoKo";
        doc["HostName"] = "PoKo";
        doc["ID"] = mac;
        doc["Instance"] = 1;
        doc["MAC"] = mac;
        doc["OS"] = "Arduino";
        doc["SnapStreamProtocolVersion"] = 2;
        doc["Version"] = "0.2.1";

        String json;
        serializeJson(doc, json);

        uint32_t strLen = json.length();
        uint32_t totalPayloadSize = 4 + strLen;
        uint8_t* pBuf = (uint8_t*)malloc(totalPayloadSize);
        if (!pBuf) return false;
        writeU32LE(pBuf, strLen);
        memcpy(pBuf + 4, json.c_str(), strLen);

        int64_t now = esp_timer_get_time();
        bool ok = sendBaseMessage(client, SNAP_MSG_HELLO, ++_timeMsgId, 0, now, totalPayloadSize, pBuf);
        free(pBuf);
        Serial.printf("[snap] Sent Hello (%u bytes payload, ok=%d)\n", totalPayloadSize, ok);
        return ok;
    }

    bool sendTimeSync(WiFiClient& client) {
        uint16_t id = ++_timeMsgId;
        int64_t sentUs = esp_timer_get_time();
        _lastTimeSentUs = sentUs;
        uint8_t payload[8] = {0};
        bool ok = sendBaseMessage(client, SNAP_MSG_TIME, id, 0, sentUs, sizeof(payload), payload);
        if (ok) rememberTimeRequest(id, sentUs);
        return ok;
    }

    bool sendClientInfo(WiFiClient& client) {
        StaticJsonDocument<128> doc;
        int volumePercent = constrain((int)lroundf(_volume * 100.0f), 0, 100);
        doc["volume"] = volumePercent;
        doc["muted"] = _serverMuted;

        String json;
        serializeJson(doc, json);

        uint32_t strLen = json.length();
        uint32_t payloadSize = 4 + strLen;
        uint8_t payload[100];

        if (payloadSize > sizeof(payload)) {
            Serial.println("[snap] ClientInfo payload unexpectedly too large");
            return false;
        }

        writeU32LE(payload, strLen);
        memcpy(payload + 4, json.c_str(), strLen);

        bool ok = sendBaseMessage(client, SNAP_MSG_CLIENT_INFO, ++_timeMsgId, 0,
                                  esp_timer_get_time(), payloadSize, payload);
        if (ok) {
            _serverVolume = volumePercent;
            _volumePublishPending = false;
            Serial.printf("[snap] Published client volume: %d%% mute=%d\n", volumePercent, _serverMuted);
        }
        return ok;
    }

    void parseServerSettings(const char* jsonStr) {
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) {
            Serial.printf("[snap] Server settings JSON error: %s\n", err.c_str());
            return;
        }

        if (doc.containsKey("bufferMs")) _serverBufferMs = doc["bufferMs"].as<int32_t>();
        if (doc.containsKey("latency")) _serverLatencyMs = doc["latency"].as<int32_t>();

        int32_t reportedVolume = _serverVolume;
        if (doc.containsKey("volume")) {
            reportedVolume = constrain(doc["volume"].as<int32_t>(), 0, 100);
            _serverVolume = reportedVolume;
        }

        if (doc.containsKey("muted")) {
            _serverMuted = doc["muted"].as<bool>();
        }

        if (!_receivedInitialServerSettings) {
            // Reconnect policy: keep the ESP's current local volume, then publish
            // it back to Snapserver/Snapweb using ClientInfo. Later ServerSettings
            // are treated as real remote-control changes and are applied locally.
            _receivedInitialServerSettings = true;
            _volumePublishPending = true;
            Serial.printf("[snap] Initial server volume=%d%%; keeping local=%d%% and advertising it\n",
                          reportedVolume, constrain((int)lroundf(_volume * 100.0f), 0, 100));
        } else if (doc.containsKey("volume")) {
            _volume = constrain(reportedVolume / 100.0f, 0.0f, 1.0f);
        }
        setScaledVolume(_serverMuted ? 0 : _serverVolume);
    }

    void parseCodecHeader(const String& codec, const uint8_t* payload, size_t size) {
        // Stream/codec changes must not leave a decoder from the previous stream alive.
        if (codec != "flac") cleanupFlacDecoder();
        if (codec != "opus") cleanupOpusDecoder();

        if (codec == "pcm") {
            if (size >= 36) {
                _channels = readU16LE(payload + 22);
                _sampleRate = readU32LE(payload + 24);
                _bitsPerSample = readU16LE(payload + 34);
            }
        } else if (codec == "flac") {
            initFlacDecoder(payload, size);
        } else if (codec == "opus") {
            if (size >= 12) {
                _sampleRate = readU32LE(payload + 4);
                _bitsPerSample = readU16LE(payload + 8);
                _channels = readU16LE(payload + 10);
                if (_sampleRate != 8000 && _sampleRate != 12000 && _sampleRate != 16000 &&
                    _sampleRate != 24000 && _sampleRate != 48000) {
                    _sampleRate = 48000;
                }
                initOpusDecoder(_sampleRate, _channels);
            }
        }
    }

    // FLAC Callbacks
    static FLAC__StreamDecoderReadStatus flacReadCb(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data) {
        SnapPlayer* self = (SnapPlayer*)client_data;
        size_t toCopy = min(*bytes, self->_flacInputRemaining);
        if (toCopy == 0) {
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
        memcpy(buffer, self->_flacInputPtr, toCopy);
        self->_flacInputPtr += toCopy;
        self->_flacInputRemaining -= toCopy;
        *bytes = toCopy;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    static FLAC__StreamDecoderWriteStatus flacWriteCb(const FLAC__StreamDecoder* decoder, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client_data) {
        SnapPlayer* self = (SnapPlayer*)client_data;
        if (!self || !self->_pcmBuf.isAllocated() || !buffer || !buffer[0]) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

        uint32_t blocksize = frame->header.blocksize;
        uint32_t channels = frame->header.channels;
        uint32_t bps = frame->header.bits_per_sample;

        if (channels > 1 && !buffer[1]) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

        self->_sampleRate = frame->header.sample_rate;
        self->_channels = channels;
        self->_bitsPerSample = bps;

        int16_t sampleBuffer[256 * 2];
        uint32_t bufIdx = 0;

        for (uint32_t i = 0; i < blocksize; i++) {
            int16_t left = 0, right = 0;
            if (bps == 16) {
                left = (int16_t)buffer[0][i];
                right = (channels > 1) ? (int16_t)buffer[1][i] : left;
            } else if (bps == 24) {
                left = (int16_t)(buffer[0][i] >> 8);
                right = (channels > 1) ? (int16_t)(buffer[1][i] >> 8) : left;
            } else if (bps == 8) {
                left = (int16_t)(buffer[0][i] << 8);
                right = (channels > 1) ? (int16_t)(buffer[1][i] << 8) : left;
            } else if (bps == 32) {
                left = (int16_t)(buffer[0][i] >> 16);
                right = (channels > 1) ? (int16_t)(buffer[1][i] >> 16) : left;
            } else {
                self->requestProducerResync("unsupported FLAC bit depth");
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            sampleBuffer[bufIdx++] = left;
            sampleBuffer[bufIdx++] = right;

            if (bufIdx >= sizeof(sampleBuffer) / sizeof(int16_t)) {
                size_t toSend = bufIdx * sizeof(int16_t);
                if (!self->writeRingExact((const uint8_t*)sampleBuffer, toSend)) {
                    // Keep the FLAC decoder alive; the rest of this encoded chunk is
                    // still consumed, but its PCM is intentionally discarded until
                    // the audio task establishes a new Snapcast timeline anchor.
                    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
                }
                bufIdx = 0;
            }
        }

        if (bufIdx > 0) {
            size_t toSend = bufIdx * sizeof(int16_t);
            if (!self->writeRingExact((const uint8_t*)sampleBuffer, toSend)) {
                return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
            }
        }

        self->_decodedFramesThisChunk += blocksize;
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void flacMetadataCb(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data) {
        SnapPlayer* self = (SnapPlayer*)client_data;
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            self->_sampleRate = metadata->data.stream_info.sample_rate;
            self->_channels = metadata->data.stream_info.channels;
            self->_bitsPerSample = metadata->data.stream_info.bits_per_sample;
        }
    }

    static void flacErrorCb(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data) {}

    void initFlacDecoder(const uint8_t* headerData, size_t headerSize) {
        cleanupFlacDecoder();
        _flacDecoder = FLAC__stream_decoder_new();
        if (!_flacDecoder) return;

        FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_stream(
            _flacDecoder,
            flacReadCb,
            NULL,
            NULL,
            NULL,
            NULL,
            flacWriteCb,
            flacMetadataCb,
            flacErrorCb,
            this
        );

        if (status == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            _flacInputPtr = headerData;
            _flacInputRemaining = headerSize;
            FLAC__stream_decoder_process_until_end_of_metadata(_flacDecoder);
        }
    }

    void cleanupFlacDecoder() {
        if (_flacDecoder) {
            FLAC__stream_decoder_finish(_flacDecoder);
            FLAC__stream_decoder_delete(_flacDecoder);
            _flacDecoder = nullptr;
        }
    }

    void initOpusDecoder(uint32_t sampleRate, uint16_t channels) {
        cleanupOpusDecoder();
        if (channels < 1 || channels > 2) channels = 2;
        if (sampleRate != 8000 && sampleRate != 12000 && sampleRate != 16000 &&
            sampleRate != 24000 && sampleRate != 48000) {
            sampleRate = 48000;
        }

        int error = OPUS_OK;
        _opusDecoder = opus_decoder_create(sampleRate, channels, &error);
        if (error != OPUS_OK || !_opusDecoder) {
            Serial.printf("[snap] opus_decoder_create failed: %d\n", error);
            _opusDecoder = nullptr;
            return;
        }

        size_t pcmBufSize = 5760 * channels * sizeof(int16_t);
        if (!_opusPcmBuf) {
            _opusPcmBuf = (int16_t*)malloc(pcmBufSize);
        }

        if (!_opusEncodedBuf) {
            _opusEncodedBuf = (uint8_t*)malloc(2048);
        }

        if (!_opusPcmBuf || !_opusEncodedBuf) {
            Serial.println("[snap] Opus buffer allocation failed");
            cleanupOpusDecoder();
            requestProducerResync("Opus buffer allocation failed");
            return;
        }

        Serial.printf("[snap] Initialized Opus decoder: %u Hz, %u ch\n", sampleRate, channels);
    }

    void cleanupOpusDecoder() {
        if (_opusDecoder) {
            opus_decoder_destroy(_opusDecoder);
            _opusDecoder = nullptr;
        }
        if (_opusPcmBuf) {
            free(_opusPcmBuf);
            _opusPcmBuf = nullptr;
        }
        if (_opusEncodedBuf) {
            free(_opusEncodedBuf);
            _opusEncodedBuf = nullptr;
        }
    }

    static void netTaskWrapper(void* param) {
        ((SnapPlayer*)param)->networkTask();
    }

    static void audioTaskWrapper(void* param) {
        ((SnapPlayer*)param)->audioTask();
    }

    void networkTask() {
        while (_isRunning) {
            WiFiClient& client = _client;
            _connected = false;
            _syncing = false;
            _playStarted = false;
            _playReleased = false;
            _receivedCodecHeader = false;
            _receivedInitialServerSettings = false;
            _volumePublishPending = false;
            _expectedNextChunkTsUs = 0;
            _producerAwaitingResync = false;
            _decodeWriteFailed = false;
            resetTimeSyncState();

            if (_serverHost.length() == 0) {
                for (int i = 0; i < 5 && _isRunning; i++) vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            Serial.printf("[snap] Connecting to %s:%u...\n", _serverHost.c_str(), _serverPort);
            if (!client.connect(_serverHost.c_str(), _serverPort, 3000)) {
                Serial.println("[snap] Connection failed. Retrying in 2s...");
                for (int i = 0; i < 20 && _isRunning; i++) vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            if (!_isRunning) {
                client.stop();
                break;
            }

            client.setNoDelay(true);
            _connected = true;
            _lastTimeSyncMs = 0;
            _chunksReceived = 0;
            _bytesDropped = 0;
            _underruns = 0;
            _i2sShortWrites = 0;
            _chunkTimestampResyncs = 0;
            _timeSyncRejects = 0;
            _timeSyncUnmatched = 0;

            if (!sendHello(client)) {
                Serial.println("[snap] sendHello failed, closing socket");
                client.stop();
                continue;
            }

            if (!sendTimeSync(client)) {
                Serial.println("[snap] Initial time sync send failed");
                client.stop();
                continue;
            }
            _lastTimeSyncMs = millis();
            uint32_t lastRxMs = millis();

            while (_isRunning && client.connected()) {
                uint32_t nowMs = millis();

                // Only the network task writes to the Snapcast socket.
                if (_volumePublishPending && _receivedInitialServerSettings) {
                    if (!sendClientInfo(client)) {
                        Serial.println("[snap] Failed to publish ClientInfo");
                        break;
                    }
                }

                uint32_t syncInterval = (_timeSyncCount < 10) ? 50 : TIME_SYNC_INTERVAL_MS;
                if (nowMs - _lastTimeSyncMs >= syncInterval) {
                    if (!sendTimeSync(client)) {
                        Serial.println("[snap] Failed to send time sync");
                        break;
                    }
                    _lastTimeSyncMs = nowMs;
                }

                if (!client.available()) {
                    if (nowMs - lastRxMs > 10000) {
                        Serial.println("[snap] Socket timeout (10s idle), reconnecting");
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(3));
                    continue;
                }

                uint8_t baseHdr[26];
                if (!readExact(client, baseHdr, sizeof(baseHdr), 4000)) {
                    Serial.println("[snap] Error reading base header");
                    break;
                }
                lastRxMs = millis();

                uint16_t msgType = readU16LE(baseHdr + 0);
                uint16_t msgId = readU16LE(baseHdr + 2);
                uint16_t msgRefersTo = readU16LE(baseHdr + 4);
                int32_t sentSec = readI32LE(baseHdr + 6);
                int32_t sentUsec = readI32LE(baseHdr + 10);
                uint32_t payloadSize = readU32LE(baseHdr + 22);
                (void)msgId;

                if (msgType == SNAP_MSG_TIME) {
                    if (payloadSize < 8) {
                        discardBytes(client, payloadSize);
                        continue;
                    }

                    uint8_t timePayload[8];
                    if (!readExact(client, timePayload, sizeof(timePayload), 2000)) break;
                    int32_t latSec = readI32LE(timePayload + 0);
                    int32_t latUsec = readI32LE(timePayload + 4);

                    int64_t requestSentUs = 0;
                    bool matched = takeTimeRequest(msgRefersTo, requestSentUs);
                    int64_t t4 = esp_timer_get_time();
                    int64_t t3 = (int64_t)sentSec * 1000000LL + (int64_t)sentUsec;
                    int64_t tdif = t4 - t3;
                    int64_t latency = (int64_t)latSec * 1000000LL + (int64_t)latUsec;
                    int64_t newDiff = (latency - tdif) / 2LL;

                    if (!matched) {
                        _timeSyncUnmatched++;
                    } else {
                        int64_t rttUs = t4 - requestSentUs;
                        if (rttUs >= 0 && rttUs <= 25000) {
                            _measuredLatencyMs = (int32_t)((rttUs + 500LL) / 1000LL);
                            addTimeDiffSample(newDiff);
                            _timeSyncCount++;
                            _syncing = true;
                            if (_timeSyncCount == 10) {
                                Serial.printf("[snap] Time sync calibrated: diff=%lld us rtt=%lld us\n",
                                              _diffToServerUs, rttUs);
                            }
                        } else {
                            _timeSyncRejects++;
                        }
                    }

                    if (payloadSize > 8 && !discardBytes(client, payloadSize - 8)) break;
                }
                else if (msgType == SNAP_MSG_SERVER_SETTINGS) {
                    if (payloadSize < 4) {
                        discardBytes(client, payloadSize);
                        continue;
                    }

                    uint8_t lenBuf[4];
                    if (!readExact(client, lenBuf, 4, 2000)) break;
                    uint32_t strLen = readU32LE(lenBuf);
                    uint32_t maxStr = payloadSize - 4;
                    if (strLen > maxStr || strLen > MAX_SETTINGS_JSON_BYTES) {
                        Serial.printf("[snap] Invalid settings JSON length: %u\n", strLen);
                        if (!discardBytes(client, maxStr)) break;
                        continue;
                    }
                    uint32_t toRead = strLen;
                    char* sBuf = (char*)malloc(toRead + 1);
                    if (!sBuf) {
                        if (!discardBytes(client, maxStr)) break;
                        continue;
                    }
                    if (!readExact(client, (uint8_t*)sBuf, toRead, 2000)) {
                        free(sBuf);
                        break;
                    }
                    sBuf[toRead] = '\0';
                    parseServerSettings(sBuf);
                    free(sBuf);
                    if (maxStr > toRead && !discardBytes(client, maxStr - toRead)) break;
                }
                else if (msgType == SNAP_MSG_CODEC_HEADER) {
                    if (payloadSize < 8) {
                        discardBytes(client, payloadSize);
                        continue;
                    }

                    uint8_t lenBuf[4];
                    if (!readExact(client, lenBuf, 4, 2000)) break;
                    uint32_t cLen = readU32LE(lenBuf);
                    if (cLen > 32 || 4 + cLen + 4 > payloadSize) {
                        Serial.printf("[snap] Invalid codec header len: %u payload=%u\n", cLen, payloadSize);
                        if (payloadSize > 4) discardBytes(client, payloadSize - 4);
                        break;
                    }

                    char cStr[33];
                    if (!readExact(client, (uint8_t*)cStr, cLen, 2000)) break;
                    cStr[cLen] = '\0';
                    _codec = String(cStr);
                    _codec.toLowerCase();

                    if (!readExact(client, lenBuf, 4, 2000)) break;
                    uint32_t subSize = readU32LE(lenBuf);
                    uint32_t consumedHeader = 4 + cLen + 4;
                    if (subSize > payloadSize - consumedHeader || subSize > MAX_CODEC_HEADER_BYTES) {
                        Serial.printf("[snap] Invalid codec subheader: %u\n", subSize);
                        discardBytes(client, payloadSize - consumedHeader);
                        break;
                    }

                    if (!ensureEncodedScratch(subSize)) {
                        if (!discardBytes(client, subSize)) break;
                        requestProducerResync("codec-header scratch allocation failed");
                    } else {
                        if (!readExact(client, _encodedChunkBuf, subSize, 3000)) break;
                        parseCodecHeader(_codec, _encodedChunkBuf, subSize);
                    }

                    uint32_t totalConsumed = consumedHeader + subSize;
                    if (payloadSize > totalConsumed && !discardBytes(client, payloadSize - totalConsumed)) break;

                    bool codecReady =
                        (_codec == "pcm" && _sampleRate > 0 && (_channels == 1 || _channels == 2)) ||
                        (_codec == "flac" && _flacDecoder != nullptr) ||
                        (_codec == "opus" && _opusDecoder != nullptr && _opusPcmBuf != nullptr);
                    _receivedCodecHeader = codecReady;
                    _expectedNextChunkTsUs = 0;
                    if (!codecReady) {
                        Serial.printf("[snap] Codec is not ready: %s\n", _codec.c_str());
                    }
                    if (_playStarted || _playReleased) requestProducerResync("codec/header change");
                    Serial.printf("[snap] Codec: %s %u Hz %u ch %u bit ready=%d\n",
                                  _codec.c_str(), _sampleRate, _channels, _bitsPerSample, codecReady);
                }
                else if (msgType == SNAP_MSG_WIRE_CHUNK) {
                    if (payloadSize < 12) {
                        discardBytes(client, payloadSize);
                        continue;
                    }

                    uint8_t chunkHdr[12];
                    if (!readExact(client, chunkHdr, sizeof(chunkHdr), 2000)) break;
                    int32_t cSec = readI32LE(chunkHdr + 0);
                    int32_t cUsec = readI32LE(chunkHdr + 4);
                    uint32_t chunkDataBytes = readU32LE(chunkHdr + 8);
                    int64_t chunkTimestampUs = (int64_t)cSec * 1000000LL + (int64_t)cUsec;
                    uint32_t availablePayload = payloadSize - 12;
                    if (chunkDataBytes > availablePayload) chunkDataBytes = availablePayload;
                    uint32_t trailingBytes = availablePayload - chunkDataBytes;

                    if (chunkDataBytes > MAX_WIRE_CHUNK_BYTES) {
                        Serial.printf("[snap] Oversized wire chunk: %u bytes\n", chunkDataBytes);
                        requestProducerResync("oversized wire chunk");
                        if (!discardBytes(client, availablePayload)) break;
                        continue;
                    }

                    if (!_receivedCodecHeader || _timeSyncCount < 10 || chunkDataBytes == 0) {
                        if (!discardBytes(client, availablePayload)) break;
                        continue;
                    }

                    // After a producer-side failure, discard complete chunks until the
                    // audio task has drained the old PCM and reset the playout anchor.
                    if (_producerAwaitingResync) {
                        if (_playStarted || _playReleased) {
                            if (!discardBytes(client, availablePayload)) break;
                            continue;
                        }
                        _producerAwaitingResync = false;
                        _expectedNextChunkTsUs = 0;
                    }

                    if (_expectedNextChunkTsUs != 0) {
                        int64_t tsErrorUs = chunkTimestampUs - _expectedNextChunkTsUs;
                        if (llabs(tsErrorUs) > CHUNK_TS_TOLERANCE_US) {
                            _chunkTimestampResyncs++;
                            Serial.printf("[snap] Chunk timestamp discontinuity: %+lld us\n", tsErrorUs);
                            requestProducerResync("chunk timestamp discontinuity");
                            if (!discardBytes(client, availablePayload)) break;
                            continue;
                        }
                    }

                    if (!_playStarted) {
                        _firstChunkServerTsUs = chunkTimestampUs;
                        int32_t effMs = getEffectiveBufferMs();
                        _targetPlayLocalTimeUs = _firstChunkServerTsUs + (int64_t)effMs * 1000LL - _diffToServerUs;
                        _samplesPlayed = 0;
                                    _playStarted = true;
                    }

                    _decodedFramesThisChunk = 0;
                    _decodeWriteFailed = false;
                    bool decodeOk = false;

                    if (_codec == "pcm") {
                        uint64_t frames = 0;
                        if (!processPcmPayload(client, chunkDataBytes, frames)) {
                            if (!client.connected()) break;
                        } else {
                            _decodedFramesThisChunk = frames;
                            decodeOk = frames > 0 && !_decodeWriteFailed;
                        }
                    }
                    else if (_codec == "flac" && _flacDecoder) {
                        if (!ensureEncodedScratch(chunkDataBytes)) {
                            if (!discardBytes(client, chunkDataBytes)) break;
                            requestProducerResync("FLAC scratch allocation failed");
                        } else {
                            if (!readExact(client, _encodedChunkBuf, chunkDataBytes, 3000)) break;
                            _flacInputPtr = _encodedChunkBuf;
                            _flacInputRemaining = chunkDataBytes;
                            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(_flacDecoder);
                            if (state == FLAC__STREAM_DECODER_END_OF_STREAM || state == FLAC__STREAM_DECODER_ABORTED) {
                                FLAC__stream_decoder_flush(_flacDecoder);
                            }
                            while (_flacInputRemaining > 0) {
                                if (!FLAC__stream_decoder_process_single(_flacDecoder)) break;
                                if (FLAC__stream_decoder_get_state(_flacDecoder) == FLAC__STREAM_DECODER_ABORTED) break;
                            }
                            FLAC__StreamDecoderState endState = FLAC__stream_decoder_get_state(_flacDecoder);
                            decodeOk = !_decodeWriteFailed && _decodedFramesThisChunk > 0 &&
                                       _flacInputRemaining == 0 && endState != FLAC__STREAM_DECODER_ABORTED;
                            if (!decodeOk && !_producerAwaitingResync) requestProducerResync("FLAC decode failed");
                        }
                    }
                    else if (_codec == "opus" && _opusDecoder && _opusPcmBuf) {
                        uint8_t* opusIn = nullptr;
                        if (chunkDataBytes <= 2048 && _opusEncodedBuf) {
                            opusIn = _opusEncodedBuf;
                        } else if (ensureEncodedScratch(chunkDataBytes)) {
                            opusIn = _encodedChunkBuf;
                        }

                        if (!opusIn) {
                            if (!discardBytes(client, chunkDataBytes)) break;
                            requestProducerResync("Opus scratch allocation failed");
                        } else {
                            if (!readExact(client, opusIn, chunkDataBytes, 3000)) break;
                            int decodedSamples = opus_decode(_opusDecoder, opusIn, chunkDataBytes, _opusPcmBuf, 5760, 0);
                            if (decodedSamples > 0) {
                                if (writeDecodedPcm(_opusPcmBuf, (uint32_t)decodedSamples, _channels)) {
                                    _decodedFramesThisChunk = (uint64_t)decodedSamples;
                                    decodeOk = !_decodeWriteFailed;
                                }
                            } else {
                                Serial.printf("[snap] opus_decode err: %d\n", decodedSamples);
                                requestProducerResync("Opus decode failed");
                            }
                        }
                    }
                    else {
                        if (!discardBytes(client, chunkDataBytes)) break;
                        requestProducerResync("unsupported codec state");
                    }

                    if (trailingBytes > 0 && !discardBytes(client, trailingBytes)) break;

                    if (decodeOk && _sampleRate > 0) {
                        _expectedNextChunkTsUs = chunkTimestampUs +
                            (int64_t)((_decodedFramesThisChunk * 1000000ULL) / _sampleRate);
                    } else {
                        _expectedNextChunkTsUs = 0;
                    }
                    _chunksReceived++;
                }
                else {
                    if (!discardBytes(client, payloadSize)) break;
                }
            }

            Serial.println("[snap] TCP connection terminated.");
            client.stop();
            _connected = false;
            _syncing = false;
            _playStarted = false;
            _playReleased = false;
            _expectedNextChunkTsUs = 0;
            _producerAwaitingResync = true;
            _resyncRequested = true;
            resetTimeSyncState();

            if (!_isRunning) break;
            for (int i = 0; i < 10 && _isRunning; i++) vTaskDelay(pdMS_TO_TICKS(100));
        }

        _connected = false;
        _syncing = false;
        _client.stop();
        _netTaskHandle = nullptr;
        if (_netTaskDone) xSemaphoreGive(_netTaskDone);
        vTaskDelete(NULL);
    }

    void audioTask() {
        uint32_t activeRate = _sampleRate ? _sampleRate : 48000;
        _audioFault = false;

        if (!initI2S(activeRate)) {
            Serial.println("[snap] Audio task could not initialize I2S");
            _audioFault = true;
            _isRunning = false;
            _client.stop();
            _audioTaskHandle = nullptr;
            if (_audioTaskDone) xSemaphoreGive(_audioTaskDone);
            vTaskDelete(NULL);
            return;
        }

        static constexpr uint32_t PCM_OUT_CAP = 257;
        int16_t pcmIn[256 * 2];
        int16_t pcmOut[PCM_OUT_CAP * 2];

        while (_isRunning) {
            if (!_pcmBuf.isAllocated()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            if (_resyncRequested) {
                _resyncRequested = false;
                _resyncGeneration++;
                _producerAwaitingResync = true;
                _pcmBuf.drain();
                _playReleased = false;
                _playStarted = false;
                _samplesPlayed = 0;
                resetPllState();
                continue;
            }

            if (_sampleRate > 0 && _sampleRate != activeRate) {
                deinitI2S();
                activeRate = _sampleRate;
                if (!initI2S(activeRate)) {
                    Serial.println("[snap] I2S re-init failed after sample-rate change");
                    _audioFault = true;
                    _isRunning = false;
                    _client.stop();
                    break;
                }
                _resyncGeneration++;
                _producerAwaitingResync = true;
                _pcmBuf.drain();
                _playReleased = false;
                _playStarted = false;
                _samplesPlayed = 0;
                resetPllState();
            }

            if (!_playReleased) {
                if (_playStarted && _timeSyncCount >= 10) {
                    int64_t nowUs = esp_timer_get_time();
                    size_t bufferedBytes = _pcmBuf.available();
                    int64_t outputDacLatencyUs =
                        (int64_t)(((uint64_t)DMA_TOTAL_FRAMES * 1000000ULL) / activeRate);
                    int64_t releaseTimeUs = _targetPlayLocalTimeUs - outputDacLatencyUs;

                    uint32_t effMs = (uint32_t)getEffectiveBufferMs();
                    uint32_t prefillMs = constrain((int)(effMs / 4U), 80, 250);
                    size_t requiredMinBytes =
                        (size_t)(((uint64_t)activeRate * 4ULL * prefillMs) / 1000ULL);
                    size_t cap = _pcmBuf.capacity();
                    if (cap > 4 && requiredMinBytes > cap - 4) requiredMinBytes = cap - 4;

                    bool timeReached = nowUs >= releaseTimeUs;
                    bool bufferTooFull = cap > 65536 && bufferedBytes >= (cap - 65536);

                    if ((timeReached && bufferedBytes >= requiredMinBytes) || bufferTooFull) {
                        _playReleased = true;
                        _samplesPlayed = 0;
                        resetPllState();
                        armStartupFade(activeRate);
                        Serial.printf("[snap] Playback released: buffered=%u prefill=%u ms lag=%lld us\n",
                                      (unsigned)bufferedBytes, (unsigned)prefillMs,
                                      nowUs - releaseTimeUs);
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(2));
                        continue;
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
            }

            size_t got = _pcmBuf.read((uint8_t*)pcmIn, sizeof(pcmIn));
            uint32_t samples = got / (sizeof(int16_t) * 2U);

            if (samples == 0) {
                if (_playReleased && !_connected) {
                    _playReleased = false;
                    _playStarted = false;
                    _producerAwaitingResync = true;
                    resetPllState();
                    continue;
                }

                if (_playReleased && _connected) {
                    // The DMA queue already contains several ms of audio. Give Wi-Fi a
                    // short grace period rather than immediately injecting silence and
                    // silently shifting the source timeline.
                    if (_consecutiveEmptyReads < 255) _consecutiveEmptyReads++;
                    if (_consecutiveEmptyReads >= EMPTY_READS_BEFORE_RESYNC) {
                        _underruns++;
                        Serial.printf("[snap] PCM underrun episode #%u; reacquiring timeline\n",
                                      (unsigned)_underruns);
                        _producerAwaitingResync = true;
                        _resyncRequested = true;
                        _consecutiveEmptyReads = 0;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            _consecutiveEmptyReads = 0;

            // Source timestamp for the next PCM frame. _samplesPlayed is 64-bit so
            // continuous playback no longer wraps after ~24.9 hours at 48 kHz.
            int64_t serverNowUs = esp_timer_get_time() + _diffToServerUs;
            int64_t nextSampleServerTsUs = _firstChunkServerTsUs +
                (int64_t)((_samplesPlayed * 1000000ULL) / activeRate);
            int64_t effectiveBufferUs = (int64_t)getEffectiveBufferMs() * 1000LL;
            int64_t outputDacLatencyUs =
                (int64_t)(((uint64_t)DMA_TOTAL_FRAMES * 1000000ULL) / activeRate);

            int64_t rawAgeUs = serverNowUs - nextSampleServerTsUs - effectiveBufferUs + outputDacLatencyUs;
            int64_t ageUs = addAgeSample(rawAgeUs);
            _lastDriftUs = (int32_t)constrain((long long)-ageUs, (long long)-2147483647LL, (long long)2147483647LL);
            _lastDriftMs = (int32_t)(-ageUs / 1000LL);

            // With the 9-sample median filled, a 50 ms phase error is no longer a
            // transient. Re-anchor instead of asking the soft PLL to repair it slowly.
            if (_ageCount >= AGE_FILTER_SIZE && llabs(ageUs) > 50000LL && _timeSyncCount >= 10) {
                Serial.printf("[snap] Hard resync: filtered age=%lld us raw=%lld us\n", ageUs, rawAgeUs);
                _resyncGeneration++;
                _producerAwaitingResync = true;
                _pcmBuf.drain();
                _playReleased = false;
                _playStarted = false;
                _samplesPlayed = 0;
                resetPllState();
                continue;
            }

            // PI soft-sync loop. The proportional term fixes phase error quickly;
            // the slow integral term learns the ESP/I2S oscillator mismatch, so after
            // minutes or hours the client can hold near-zero phase error instead of
            // requiring a permanent several-ms offset to generate a correction.
            double errorMs = (double)ageUs / 1000.0;
            double blockSeconds = (double)samples / (double)activeRate;
            if (_ageCount >= AGE_FILTER_SIZE && fabs(errorMs) < 25.0) {
                _pllIntegralPpm += errorMs * 0.8 * blockSeconds;
                _pllIntegralPpm = constrain(_pllIntegralPpm, -350.0, 350.0);
            }

            double phasePpm = (llabs(ageUs) < 80LL) ? 0.0 : errorMs * 18.0;
            double correctionPpm = constrain(phasePpm + _pllIntegralPpm, -500.0, 500.0);
            _lastCorrectionPpm = correctionPpm;
            _correctionAccumulator += correctionPpm * 1e-6 * (double)samples;

            bool dropFrame = false;
            bool dupFrame = false;
            if (_correctionAccumulator >= 1.0) {
                dropFrame = true;
                _correctionAccumulator -= 1.0;
            } else if (_correctionAccumulator <= -1.0) {
                dupFrame = true;
                _correctionAccumulator += 1.0;
            }

            float v = _serverMuted ? 0.0f : _volume;
            uint32_t outFrames = 0;
            uint32_t correctionPos = samples / 2U;

            for (uint32_t i = 0; i < samples; i++) {
                int16_t left = pcmIn[i * 2];
                int16_t right = pcmIn[i * 2 + 1];

                // Gain-only startup ramp: no silence is inserted and no frames are
                // delayed, so Snapcast timing remains exact. Smoothstep prevents
                // the first arbitrary waveform sample from becoming a full-scale step.
                float fadeGain = 1.0f;
                if (_fadeFramesDone < _fadeFramesTotal) {
                    float x = (float)_fadeFramesDone / (float)_fadeFramesTotal;
                    fadeGain = x * x * (3.0f - 2.0f * x);
                }
                float gain = v * fadeGain;

                if (dropFrame && i == correctionPos) {
                    if (_fadeFramesDone < _fadeFramesTotal) _fadeFramesDone++;
                    continue;
                }

                if (outFrames < PCM_OUT_CAP) {
                    pcmOut[outFrames * 2] =
                        (int16_t)constrain((int32_t)((float)left * gain), -32768, 32767);
                    pcmOut[outFrames * 2 + 1] =
                        (int16_t)constrain((int32_t)((float)right * gain), -32768, 32767);
                    outFrames++;
                }

                if (dupFrame && i == correctionPos && outFrames < PCM_OUT_CAP) {
                    // Insert an interpolated frame rather than an exact duplicate to
                    // reduce the tiny discontinuity caused by sample stuffing.
                    int16_t nextL = (i + 1 < samples) ? pcmIn[(i + 1) * 2] : left;
                    int16_t nextR = (i + 1 < samples) ? pcmIn[(i + 1) * 2 + 1] : right;
                    int32_t interpL = ((int32_t)left + (int32_t)nextL) / 2;
                    int32_t interpR = ((int32_t)right + (int32_t)nextR) / 2;
                    pcmOut[outFrames * 2] =
                        (int16_t)constrain((int32_t)((float)interpL * gain), -32768, 32767);
                    pcmOut[outFrames * 2 + 1] =
                        (int16_t)constrain((int32_t)((float)interpR * gain), -32768, 32767);
                    outFrames++;
                }

                if (_fadeFramesDone < _fadeFramesTotal) _fadeFramesDone++;
            }

            if (outFrames > 0) {
                const size_t requestedBytes = outFrames * sizeof(int16_t) * 2U;
                size_t written = 0;
                esp_err_t err = poko_tx_handle ? i2s_channel_write(poko_tx_handle, pcmOut, requestedBytes, &written, pdMS_TO_TICKS(30)) : ESP_FAIL;
                if (err != ESP_OK || written != requestedBytes) {
                    _i2sShortWrites++;
                    Serial.printf("[snap] I2S short write: err=%d %u/%u\n",
                                  (int)err, (unsigned)written, (unsigned)requestedBytes);
                    _producerAwaitingResync = true;
                    _resyncRequested = true;
                    continue;
                }
            }

            // Advance the authoritative source timeline, not the number of frames
            // emitted to the DAC after sample stuffing.
            _samplesPlayed += (uint64_t)samples;
        }

        _audioTaskHandle = nullptr;
        if (_audioTaskDone) xSemaphoreGive(_audioTaskDone);
        vTaskDelete(NULL);
    }

public:
    SnapPlayer(void* display = nullptr, Preferences* prefs = nullptr, float initialVolume = 0.8f)
        : _tft(display), _prefs(prefs), _serverHost("192.168.0.20"), _serverPort(1704), _customLatencyMs(0),
          _volume(initialVolume), _isRunning(false), _isLoaded(false), _connected(false),
          _syncing(false), _playStarted(false), _playReleased(false), _netTaskHandle(NULL),
          _audioTaskHandle(NULL), _netTaskDone(nullptr), _audioTaskDone(nullptr),
          _netTaskStarted(false), _audioTaskStarted(false), _i2sInstalled(false),
          _resyncRequested(false), _volumePublishPending(false),
          _receivedInitialServerSettings(false), _audioFault(false),
          _codec("opus"), _sampleRate(48000), _channels(2), _bitsPerSample(16),
          _serverBufferMs(1000), _serverLatencyMs(0), _measuredLatencyMs(0), _serverVolume(80), _serverMuted(false),
          _diffCount(0), _diffIdx(0), _ageCount(0), _ageIdx(0),
          _correctionAccumulator(0.0), _pllIntegralPpm(0.0), _lastCorrectionPpm(0.0),
          _diffToServerUs(0), _lastTimeSyncMs(0), _lastTimeSentUs(0), _timeMsgId(0),
          _firstChunkServerTsUs(0), _targetPlayLocalTimeUs(0), _expectedNextChunkTsUs(0),
          _samplesPlayed(0), _decodedFramesThisChunk(0), _chunksReceived(0), _bytesDropped(0),
          _underruns(0), _i2sShortWrites(0), _chunkTimestampResyncs(0),
          _timeSyncRejects(0), _timeSyncUnmatched(0), _consecutiveEmptyReads(0),
          _lastDriftMs(0), _lastDriftUs(0), _producerAwaitingResync(false), _decodeWriteFailed(false), _resyncGeneration(0),
          _fadeFramesTotal(1), _fadeFramesDone(1),
          _lastOverlayUpdateMs(0), _timeSyncCount(0), _receivedCodecHeader(false),
          _flacDecoder(nullptr), _flacInputPtr(nullptr), _flacInputRemaining(0),
          _opusDecoder(nullptr), _opusPcmBuf(nullptr), _opusEncodedBuf(nullptr),
          _encodedChunkBuf(nullptr), _encodedChunkCap(0),
          _overlayStaticDrawn(false), _uiVolumeFillW(-1), _uiVolumeMuted(false),
          _knobMode(KNOB_VOLUME) {
        memset(_diffHistory, 0, sizeof(_diffHistory));
        memset(_ageHistory, 0, sizeof(_ageHistory));
        memset(_timeRequests, 0, sizeof(_timeRequests));
    }


    void begin() {
        if (!_netTaskDone) _netTaskDone = xSemaphoreCreateBinary();
        if (!_audioTaskDone) _audioTaskDone = xSemaphoreCreateBinary();

        if (!_netTaskDone || !_audioTaskDone) {
            Serial.println("[snap] WARNING: failed to allocate task completion semaphores");
        }

        if (_prefs) {
            _serverHost = _prefs->getString("snap_host", "192.168.0.20");
            _serverPort = (uint16_t)_prefs->getInt("snap_port", 1704);
            _customLatencyMs = _prefs->getInt("snap_lat", 0);
            _volume = constrain(_prefs->getInt("volume", 75) / 100.0f, 0.0f, 1.0f);
        }
    }

    void load() {
        if (_isLoaded) return;

        size_t bufSize = psramFound() ? RING_BUFFER_SIZE : 49152;
        if (!_pcmBuf.init(bufSize)) {
            size_t fallback = psramFound() ? 262144 : 32768;
            if (!_pcmBuf.init(fallback)) {
                Serial.println("[snap] Failed to allocate PCM ring buffer!");
                return;
            }
        }

        if (!_netTaskDone) _netTaskDone = xSemaphoreCreateBinary();
        if (!_audioTaskDone) _audioTaskDone = xSemaphoreCreateBinary();

        if (!_netTaskDone || !_audioTaskDone) {
            Serial.println("[snap] Failed to allocate task completion semaphores");
            _pcmBuf.freeBuffer();
            return;
        }

        xSemaphoreTake(_netTaskDone, 0);
        xSemaphoreTake(_audioTaskDone, 0);
        _netTaskStarted = false;
        _audioTaskStarted = false;

        _isRunning = true;
        _isLoaded = true;
        _playStarted = false;
        _playReleased = false;
        _samplesPlayed = 0;
        _decodedFramesThisChunk = 0;
        _chunksReceived = 0;
        _bytesDropped = 0;
        _underruns = 0;
        _i2sShortWrites = 0;
        _chunkTimestampResyncs = 0;
        _timeSyncRejects = 0;
        _timeSyncUnmatched = 0;
        _receivedCodecHeader = false;
        _receivedInitialServerSettings = false;
        _resyncRequested = false;
        _volumePublishPending = false;
        _producerAwaitingResync = false;
        _decodeWriteFailed = false;
        _resyncGeneration = 0;
        _fadeFramesTotal = 1;
        _fadeFramesDone = 1;
        _expectedNextChunkTsUs = 0;
        _audioFault = false;
        _overlayStaticDrawn = false;
        resetTimeSyncState();
        resetPllState();
        resetOverlayCache();

        // Pin both tasks to Core 0 (leaving Core 1 for Arduino loopTask, WebServer, and TFT SPI)
        if (xTaskCreatePinnedToCore(netTaskWrapper, "snapNet", 32768, this, 2, &_netTaskHandle, 0) == pdPASS) {
            _netTaskStarted = true;
        } else {
            _netTaskHandle = nullptr;
            Serial.println("[snap] Failed to create network task");
        }

        if (xTaskCreatePinnedToCore(audioTaskWrapper, "snapAudio", 8192, this, 3, &_audioTaskHandle, 0) == pdPASS) {
            _audioTaskStarted = true;
        } else {
            _audioTaskHandle = nullptr;
            Serial.println("[snap] Failed to create audio task");
        }

        if (!_netTaskStarted || !_audioTaskStarted) {
            Serial.println("[snap] Startup incomplete; shutting SnapPlayer down");
            unload();
        }
    }

    void unload() {
        if (!_isLoaded) return;
        Serial.println("[snap] unload: stopping");

        // Tell workers to exit.
        _isRunning = false;

        // Instantly unblock WiFi reads
        _client.stop();

        // Wait for each worker to confirm it reached its shutdown path.
        bool netStopped = true;
        if (_netTaskStarted && _netTaskDone != nullptr) {
            netStopped = (xSemaphoreTake(_netTaskDone, pdMS_TO_TICKS(5000)) == pdTRUE);
        }

        bool audioStopped = true;
        if (_audioTaskStarted && _audioTaskDone != nullptr) {
            audioStopped = (xSemaphoreTake(_audioTaskDone, pdMS_TO_TICKS(5000)) == pdTRUE);
        }

        Serial.printf("[snap] unload workers: net=%d audio=%d\n", netStopped, audioStopped);

        if (!netStopped || !audioStopped) {
            Serial.println("[snap] ERROR: worker failed to stop cleanly, skipping dealloc");
            return;
        }

        cleanupFlacDecoder();
        cleanupOpusDecoder();
        cleanupEncodedScratch();

        deinitI2S();

        _pcmBuf.freeBuffer();

        _isLoaded = false;
        _connected = false;
        _syncing = false;
        _playStarted = false;
        _playReleased = false;
        _overlayStaticDrawn = false;
        resetOverlayCache();
        _netTaskStarted = false;
        _audioTaskStarted = false;
        _resyncRequested = false;
        _volumePublishPending = false;
        _receivedInitialServerSettings = false;
        _producerAwaitingResync = false;
        _decodeWriteFailed = false;
        _expectedNextChunkTsUs = 0;
        resetTimeSyncState();
        resetPllState();
        _audioFault = false;

        Serial.println("[snap] unload complete");
    }

    bool isLoaded() const { return _isLoaded; }
    bool isConnected() const { return _connected; }

    void setVolume(float vol) {
        float next = constrain(vol, 0.0f, 1.0f);
        if (fabsf(next - _volume) < 0.0005f) return;

        _volume = next;
        if (_connected) _volumePublishPending = true;
    }

    void adjustVolume(int8_t delta) {
        setVolume(_volume + (delta * 0.05f));
        redrawOverlay();
    }

    void setServer(const String& host) {
        _serverHost = host;
    }

    void setPort(uint16_t port) {
        _serverPort = port;
    }

    int32_t getCustomLatency() const { return _customLatencyMs; }

    void setCustomLatency(int32_t lat) {
        int32_t next = constrain(lat, -2000, 2000);
        if (next == _customLatencyMs) return;

        _customLatencyMs = next;
        if (_prefs) _prefs->putInt("snap_lat", _customLatencyMs);

        // snapAudio is the sole ring-buffer consumer, so request the drain there.
        _resyncRequested = true;
    }

    void adjustCustomLatency(int32_t deltaMs) {
        setCustomLatency(_customLatencyMs + deltaMs);
        redrawOverlay();
    }

    void toggleKnobMode() {
        _knobMode = (_knobMode == KNOB_VOLUME) ? KNOB_LATENCY : KNOB_VOLUME;
        redrawOverlay();
    }

    void handleRotate(int8_t delta) {
        if (_knobMode == KNOB_LATENCY) {
            adjustCustomLatency(delta * 10); // 10ms per click
        } else {
            adjustVolume(delta);
        }
    }

    String getServer() const { return _serverHost; }
    uint16_t getPort() const { return _serverPort; }

    String getStatusJSON() const {
        String json = "{";
        json += "\"loaded\":" + String(_isLoaded ? "true" : "false") + ",";
        json += "\"connected\":" + String(_connected ? "true" : "false") + ",";
        json += "\"syncing\":" + String(_syncing ? "true" : "false") + ",";
        json += "\"server\":\"" + _serverHost + "\",";
        json += "\"port\":" + String(_serverPort) + ",";
        json += "\"codec\":\"" + _codec + "\",";
        json += "\"sample_rate\":" + String(_sampleRate) + ",";
        json += "\"channels\":" + String(_channels) + ",";
        json += "\"bits\":" + String(_bitsPerSample) + ",";
        json += "\"buffer_ms\":" + String(_serverBufferMs) + ",";
        json += "\"server_latency_ms\":" + String(_serverLatencyMs) + ",";
        json += "\"custom_latency_ms\":" + String(_customLatencyMs) + ",";
        json += "\"effective_buffer_ms\":" + String(getEffectiveBufferMs()) + ",";
        json += "\"diff_s\":" + String((int32_t)(_diffToServerUs / 1000000LL)) + ",";
        json += "\"diff_us\":" + String((long long)_diffToServerUs) + ",";
        json += "\"drift_us\":" + String(_lastDriftUs) + ",";
        json += "\"drift_ms\":" + String(_lastDriftMs) + ",";
        json += "\"correction_ppm\":" + String(_lastCorrectionPpm, 2) + ",";
        json += "\"pll_integral_ppm\":" + String(_pllIntegralPpm, 2) + ",";
        json += "\"source_frames\":" + u64String(_samplesPlayed) + ",";
        json += "\"startup_fade_ms\":" + String(STARTUP_FADE_MS) + ",";
        json += "\"i2s_prime_ms\":" + String(I2S_PRIME_MS) + ",";
        json += "\"chunks\":" + String(_chunksReceived) + ",";
        json += "\"bytes_dropped\":" + String(_bytesDropped) + ",";
        json += "\"underruns\":" + String(_underruns) + ",";
        json += "\"i2s_short_writes\":" + String(_i2sShortWrites) + ",";
        json += "\"timestamp_resyncs\":" + String(_chunkTimestampResyncs) + ",";
        json += "\"timesync_rejects\":" + String(_timeSyncRejects) + ",";
        json += "\"timesync_unmatched\":" + String(_timeSyncUnmatched) + ",";
        json += "\"timesync_samples\":" + String(_timeSyncCount) + ",";
        json += "\"pcm_buffer_bytes\":" + String((unsigned long)_pcmBuf.available()) + ",";
        json += "\"pcm_capacity_bytes\":" + String((unsigned long)_pcmBuf.capacity()) + ",";
        json += "\"audio_fault\":" + String(_audioFault ? "true" : "false") + ",";
        json += "\"volume\":" + String((int)lroundf(_volume * 100.0f)) + ",";
        json += "\"server_volume\":" + String(_serverVolume) + ",";
        json += "\"muted\":" + String(_serverMuted ? "true" : "false") + ",";
        json += "\"play_released\":" + String(_playReleased ? "true" : "false");
        json += "}";
        return json;
    }

    void recover() {
        if (!_isLoaded) return;

        unload();
        if (!_isLoaded) {
            load();
        } else {
            Serial.println("[snap] recover aborted: previous instance did not stop cleanly");
        }
    }

    void update() {
        // Audio tasks run asynchronously in background FreeRTOS tasks
    }

    void redrawOverlay() {}


    // ── Public Accessors for SSyncApp & WebUI ──────────────────
    bool isPlaying() const { return _playStarted && _connected; }
    bool isSyncing() const { return _syncing; }
    int  getVolume() const { return (int)lroundf(_volume * 100.0f); }
    bool isMuted() const { return _serverMuted; }
    String getCodec() const { return _codec; }
    uint32_t getSampleRate() const { return _sampleRate; }
    int32_t getBufferMs() const { return _serverBufferMs; }
    int32_t getLatencyMs() const {
        if (_measuredLatencyMs > 0) return _measuredLatencyMs;
        if (_serverLatencyMs > 0) return _serverLatencyMs;
        return _customLatencyMs;
    }
    String getServerHost() const { return _serverHost; }
    uint16_t getServerPort() const { return _serverPort; }

    void setVolumePercent(int pct) {
        pct = constrain(pct, 0, 100);
        _volume = pct / 100.0f;
        _serverVolume = pct;
        setScaledVolume(_serverMuted ? 0 : _serverVolume);
        _volumePublishPending = true;
    }

    void toggleMute() {
        _serverMuted = !_serverMuted;
        setScaledVolume(_serverMuted ? 0 : _serverVolume);
        _volumePublishPending = true;
    }

    void setServer(const String& host, uint16_t port) {
        _serverHost = host;
        _serverPort = port;
        if (_prefs) {
            _prefs->putString("snap_host", host);
            _prefs->putInt("snap_port", port);
        }
    }
};
