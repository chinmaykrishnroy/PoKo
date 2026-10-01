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

#include <SPIFFS.h>
#include <ESP8266Audio.h>
#include <atomic>

extern "C" {
#include "libflac/FLAC/stream_decoder.h"
#include "libopus/include/opus.h"
}

extern String getNetworkStatusMsg();

// Lock-free Single-Producer / Single-Consumer ring buffer.
// snapNet task is the SOLE writer (updates _head).
// snapAudio task is the SOLE reader (updates _tail).
// Synchronized using std::atomic with acquire-release memory order.
class SnapAudioRingBuffer {
private:
    uint8_t*            _buffer;
    size_t              _capacity;
    std::atomic<size_t> _head{0};  // written only by producer
    std::atomic<size_t> _tail{0};  // written only by consumer

public:
    SnapAudioRingBuffer();

    ~SnapAudioRingBuffer();

    bool init(size_t size);

    void freeBuffer();

    // Reset — call only when both producer and consumer tasks are stopped.
    void reset();

    // Thread-safe drain for consumer task — discards buffered audio without racing producer
    void drain();

    bool isAllocated() const;

    size_t capacity() const;

    // Bytes available to read (always 4-byte frame aligned).
    size_t available() const;

    // Free bytes available to write (always 4-byte frame aligned, leaving 4-byte gap).
    size_t freeSpace() const;

    // Write up to len bytes. Guaranteed strictly 4-byte aligned to prevent sample phase corruption.
    size_t write(const uint8_t* data, size_t len);

    // Read up to len bytes into dest. Guaranteed strictly 4-byte aligned.
    size_t read(uint8_t* dest, size_t len);
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
    volatile bool _isSuspended = false;
    volatile bool _suspendDrainRequested = false;
    volatile bool _suspendDeinitRequested = false;
    typedef bool (*AudioActiveFn)();
    typedef void (*AudioReleaseFn)();
    typedef void (*AudioVolumeChangeFn)(int vol, bool muted);
    AudioActiveFn _isAudioActiveFn = nullptr;
    AudioReleaseFn _releaseAudioFn = nullptr;
    AudioVolumeChangeFn _onVolumeChangeFn = nullptr;

    TaskHandle_t _netTaskHandle;
    TaskHandle_t _audioTaskHandle;
    SemaphoreHandle_t _netTaskDone;
    SemaphoreHandle_t _audioTaskDone;
    SemaphoreHandle_t _suspendDone;
    SemaphoreHandle_t _audioReady;
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

    static const int DMA_BUF_COUNT = POKO_I2S_DMA_DESC_NUM;
    static const int DMA_BUF_LEN = POKO_I2S_DMA_FRAME_NUM;
    static const int DMA_TOTAL_FRAMES = POKO_I2S_DMA_BUFFER_FRAMES; // 1440 frames (~30ms @ 48kHz)

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

    int32_t getEffectiveBufferMs() const;

    void addTimeDiffSample(int64_t diff);

    void resetTimeSyncState();

    void resetPllState();

    int64_t addAgeSample(int64_t ageUs);

    void rememberTimeRequest(uint16_t id, int64_t sentUs);

    bool takeTimeRequest(uint16_t id, int64_t& sentUs);

    void requestProducerResync(const char* reason);

    bool writeRingExact(const uint8_t* data, size_t bytes);

    bool ensureEncodedScratch(size_t bytes);

    void cleanupEncodedScratch();

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

    void resetOverlayCache();

    String fitUiText(const String& text, int maxWidth, uint8_t font);

    void drawCachedUiLine(String& cacheKey, const String& text, int x, int y,
                          int width, uint16_t color, uint8_t font = 2);

    static uint16_t readU16LE(const uint8_t* p);
    static uint32_t readU32LE(const uint8_t* p);
    static int32_t readI32LE(const uint8_t* p);

    static void writeU16LE(uint8_t* p, uint16_t v);
    static void writeU32LE(uint8_t* p, uint32_t v);
    static void writeI32LE(uint8_t* p, int32_t v);

    static String u64String(uint64_t value);

    void muteI2SPins();

    bool primeI2SPath(uint32_t sampleRate);

    void armStartupFade(uint32_t sampleRate);

    bool initI2S(uint32_t sampleRate);

    void deinitI2S();

    bool readExact(WiFiClient& client, uint8_t* dest, size_t len, uint32_t timeoutMs = 3000);

    bool discardBytes(WiFiClient& client, size_t len, uint32_t timeoutMs = 3000);

    int16_t decodePcmSample(const uint8_t* p, uint16_t bits) const;

    bool writeDecodedPcm(const int16_t* pcm, uint32_t frames, uint16_t channels);

    bool processPcmPayload(WiFiClient& client, uint32_t payloadBytes, uint64_t& framesOut);

    bool writeAll(WiFiClient& client, const uint8_t* data, size_t len, uint32_t timeoutMs = 2000);

    bool sendBaseMessage(WiFiClient& client, uint16_t type, uint16_t id, uint16_t refersTo,
                         int64_t sentUs, uint32_t payloadSize, const uint8_t* payload = nullptr);

    bool sendHello(WiFiClient& client);

    bool sendTimeSync(WiFiClient& client);

    bool sendClientInfo(WiFiClient& client);

    void parseServerSettings(const char* jsonStr);

    void parseCodecHeader(const String& codec, const uint8_t* payload, size_t size);

    // FLAC Callbacks
    static FLAC__StreamDecoderReadStatus flacReadCb(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data);

    static FLAC__StreamDecoderWriteStatus flacWriteCb(const FLAC__StreamDecoder* decoder, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client_data);

    static void flacMetadataCb(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data);

    static void flacErrorCb(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data);

    void initFlacDecoder(const uint8_t* headerData, size_t headerSize);

    void cleanupFlacDecoder();

    void initOpusDecoder(uint32_t sampleRate, uint16_t channels);

    void cleanupOpusDecoder();

    static void netTaskWrapper(void* param);

    static void audioTaskWrapper(void* param);

    void networkTask();

    void audioTask();

public:
    SnapPlayer(void* display = nullptr, Preferences* prefs = nullptr, float initialVolume = 0.8f);


    void begin();

    void load(bool startSuspended = false);

    void shutdownWorkersAndResources();

    void unload();

    bool isLoaded() const;
    bool isConnected() const;

    void setVolume(float vol);

    void adjustVolume(int8_t delta);

    void setServer(const String& host);

    void setPort(uint16_t port);

    int32_t getCustomLatency() const;

    void setCustomLatency(int32_t lat);

    void adjustCustomLatency(int32_t deltaMs);

    void toggleKnobMode();

    void handleRotate(int8_t delta);

    String getServer() const;
    uint16_t getPort() const;

    String getStatusJSON() const;

    void recover();

    void update();

    void redrawOverlay();


    // ── Public Accessors for SSyncApp & WebUI ──────────────────
    void setAudioCallbacks(AudioActiveFn activeFn, AudioReleaseFn releaseFn, AudioVolumeChangeFn volFn = nullptr);

    bool isAudioActive() const;

    bool isPlaying() const;
    bool isSuspended() const;
    bool isSyncing() const;
    int  getVolume() const;
    bool isMuted() const;

    bool isWorkersHealthy() const;

    bool waitForAudioReady(uint32_t timeoutMs = 500);

    bool waitForSuspend(uint32_t timeoutMs = 200);

    void suspendAudio();

    void resumeAudio();

    void stop();
    String getCodec() const;
    uint32_t getSampleRate() const;
    int32_t getBufferMs() const;
    int32_t getLatencyMs() const;
    String getServerHost() const;
    uint16_t getServerPort() const;

    void setRemoteVolumePercent(int pct);

    void setVolumePercent(int pct);

    void toggleMute();

    void setMute(bool mute);

    void setServer(const String& host, uint16_t port);
};

