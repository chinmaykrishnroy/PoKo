#pragma once
#include <WiFi.h>
#include <TJpg_Decoder.h>
#include <Arduino_GFX_Library.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>
#include <atomic>
#include "PokoDrivers.h"

// ─────────────────────────────────────────────────────────────
//  SyncedAVPlayer — 128×128 Dual-TCP Synchronized Video & Audio
//  Follows the proven PoKo NAV1 packet protocol:
//    - Port 1236: 22,050 Hz 16-bit Mono PCM audio
//    - Port 1237: MJPEG video frames synchronized to audio clock
//  Decodes via TJpg_Decoder directly to GC9107 display via Arduino_GFX
//  and outputs audio to ES8311 codec via poko_tx_handle (I2S).
// ─────────────────────────────────────────────────────────────

class SyncedAVPlayer {
private:
    static const uint32_t MAGIC              = 0x3156414E; // "NAV1" little-endian
    static const uint8_t  PACKET_AUDIO       = 1;
    static const uint8_t  PACKET_VIDEO       = 2;
    static const uint16_t AUDIO_RATE         = 22050;
    static const uint16_t AUDIO_CHUNK_BYTES  = 512;
    static const uint16_t VIDEO_QUEUE_DEPTH  = 8;
    static const size_t   VIDEO_BUFFER_SIZE  = 16384;      // Up to 16KB per 128×128 frame
    static const size_t   AUDIO_BUFFER_BYTES = 32768;
    static const size_t   START_AUDIO_BYTES  = 4410;
    static const int16_t  VIDEO_EARLY_MS     = 14;
    static const uint16_t VIDEO_LATE_DROP_MS = 250;

    struct VideoFrame {
        uint8_t* buffer;
        uint32_t length;
        uint32_t timestampMs;
    };

    Arduino_GFX*    _gfx;
    uint16_t        _port;
    WiFiServer      _server;
    WiFiServer      _videoServer;

    std::atomic<bool> _isRunning{false};
    bool            _isLoaded;
    std::atomic<bool> _audioConnected{false};
    std::atomic<bool> _videoConnected{false};
    bool            _wasConnected;
    std::atomic<float> _volume{1.0f};
    bool            _allocationFailed;
    uint32_t        _disconnectStartMs;

    TaskHandle_t         _netTaskHandle;
    TaskHandle_t         _videoTaskHandle;
    TaskHandle_t         _audioTaskHandle;
    SemaphoreHandle_t    _netTaskDone = nullptr;
    SemaphoreHandle_t    _videoTaskDone = nullptr;
    SemaphoreHandle_t    _audioTaskDone = nullptr;
    QueueHandle_t        _videoQueue;
    QueueHandle_t        _emptyQueue;
    StreamBufferHandle_t _audioStream;
    uint8_t**            _videoBuffers;

    std::atomic<bool>     _playStarted{false};
    std::atomic<bool>     _playReleased{false};
    std::atomic<uint32_t> _firstAudioTsMs{0};
    std::atomic<uint32_t> _samplesPlayed{0};
    std::atomic<uint32_t> _wallClockStartMs{0};

    std::atomic<uint32_t> _audioPackets{0};
    std::atomic<uint32_t> _audioBytesDropped{0};
    std::atomic<uint32_t> _audioUnderruns{0};
    std::atomic<uint32_t> _videoPackets{0};
    std::atomic<uint32_t> _videoFramesDropped{0};
    uint32_t          _videoFramesRendered;
    uint32_t          _lastFpsSampleMs;
    uint32_t          _lastFpsFrameCount;
    float             _renderFps;

    static SyncedAVPlayer* _instance;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

    static void networkTaskWrapper(void* arg);

    static void videoTaskWrapper(void* arg);

    static void audioTaskWrapper(void* arg);

    static uint32_t readU32LE(const uint8_t* p);

    bool readExact(WiFiClient& client, uint8_t* dst, size_t len, uint32_t idleTimeoutMs = 3000);

    bool discardBytes(WiFiClient& client, uint32_t len);

    void resetPlaybackState();

    void audioNetworkTask();

    void videoNetworkTask();

    void audioTask();

    void releaseResources();

public:
    SyncedAVPlayer(Arduino_GFX* display, uint16_t port = 1236, float initialVolume = 1.0f);

    void setVolume(float vol);

    bool isLoaded() const;
    bool isRunning() const;
    bool hasStarted() const;
    bool isConnected() const;
    bool isPlaying() const;
    float getRenderFps() const;

    uint32_t audioClockMs() const;

    bool hasFinished();

    void reset();

    void load();

    void unload();

    void update();
};

