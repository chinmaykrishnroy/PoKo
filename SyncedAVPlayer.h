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
#include "PokoDrivers.h"

// ─────────────────────────────────────────────────────────────
//  SyncedAVPlayer — 128×128 Dual-TCP Synchronized Video & Audio
//  Follows the proven Nexus NAV1 packet protocol:
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
    static const uint16_t VIDEO_QUEUE_DEPTH  = 3;
    static const size_t   VIDEO_BUFFER_SIZE  = 16384;      // Up to 16KB per 128×128 frame
    static const size_t   AUDIO_BUFFER_BYTES = 32768;
    static const size_t   START_AUDIO_BYTES  = 4000;
    static const int16_t  VIDEO_EARLY_MS     = 14;
    static const uint16_t VIDEO_LATE_DROP_MS = 150;

    struct VideoFrame {
        uint8_t* buffer;
        uint32_t length;
        uint32_t timestampMs;
    };

    Arduino_GFX*    _gfx;
    uint16_t        _port;
    WiFiServer      _server;
    WiFiServer      _videoServer;

    volatile bool   _isRunning;
    bool            _isLoaded;
    volatile bool   _clientConnected;
    volatile bool   _audioConnected;
    volatile bool   _videoConnected;
    bool            _wasConnected;
    volatile float  _volume;
    bool            _allocationFailed;

    TaskHandle_t         _netTaskHandle;
    TaskHandle_t         _videoTaskHandle;
    TaskHandle_t         _audioTaskHandle;
    QueueHandle_t        _videoQueue;
    QueueHandle_t        _emptyQueue;
    StreamBufferHandle_t _audioStream;
    uint8_t**            _videoBuffers;

    volatile bool     _playStarted;
    volatile bool     _playReleased;
    volatile uint32_t _firstAudioTsMs;
    volatile uint32_t _samplesPlayed;

    volatile uint32_t _audioPackets;
    volatile uint32_t _audioBytesDropped;
    volatile uint32_t _audioUnderruns;
    volatile uint32_t _videoPackets;
    volatile uint32_t _videoFramesDropped;
    uint32_t          _videoFramesRendered;
    uint32_t          _lastFpsSampleMs;
    uint32_t          _lastFpsFrameCount;
    float             _renderFps;

    static SyncedAVPlayer* _instance;

    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (!_instance || !_instance->_isLoaded) return false;
        if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

    static void networkTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->audioNetworkTask();
    }

    static void videoTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->videoNetworkTask();
    }

    static void audioTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->audioTask();
    }

    static uint32_t readU32LE(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    bool readExact(WiFiClient& client, uint8_t* dst, size_t len, uint32_t idleTimeoutMs = 3000) {
        size_t got = 0;
        uint32_t lastDataMs = millis();
        while (got < len && _isRunning && client.connected()) {
            int available = client.available();
            if (available > 0) {
                int n = client.read(dst + got, min((size_t)available, len - got));
                if (n > 0) {
                    got += n;
                    lastDataMs = millis();
                }
            } else {
                if (millis() - lastDataMs > idleTimeoutMs) break;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        return got == len;
    }

    bool discardBytes(WiFiClient& client, uint32_t len) {
        uint8_t scratch[256];
        while (len > 0 && _isRunning && client.connected()) {
            size_t chunk = min((uint32_t)sizeof(scratch), len);
            if (!readExact(client, scratch, chunk)) return false;
            len -= chunk;
        }
        return true;
    }

    void resetPlaybackState() {
        if (_audioStream) xStreamBufferReset(_audioStream);
        if (_videoQueue && _emptyQueue) {
            VideoFrame f;
            while (xQueueReceive(_videoQueue, &f, 0) == pdTRUE) {
                uint8_t* ptr = f.buffer;
                xQueueSend(_emptyQueue, &ptr, 0);
            }
        }
        _playStarted = false;
        _playReleased = false;
        _firstAudioTsMs = 0;
        _samplesPlayed = 0;
        _audioPackets = 0;
        _audioBytesDropped = 0;
        _audioUnderruns = 0;
        _videoPackets = 0;
        _videoFramesDropped = 0;
        _videoFramesRendered = 0;
        _lastFpsSampleMs = millis();
        _lastFpsFrameCount = 0;
        _renderFps = 0.0f;
    }

    void audioNetworkTask() {
        _server.begin();
        _server.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _server.available();
            if (!client) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            client.setNoDelay(true);
            _audioConnected = true;
            _clientConnected = true;
            resetPlaybackState();
            bool receivedPacket = false;

            while (_isRunning && client.connected()) {
                uint8_t header[16];
                if (!readExact(client, header, sizeof(header), receivedPacket ? 3000 : 8000)) break;

                uint32_t magic = readU32LE(header);
                uint8_t type = header[4];
                uint32_t timestampMs = readU32LE(header + 8);
                uint32_t length = readU32LE(header + 12);

                if (magic != MAGIC || length > 65535) break;

                if (type == PACKET_AUDIO) {
                    uint8_t audioBuf[1024];
                    uint32_t remaining = length;
                    if (!_playStarted) {
                        _firstAudioTsMs = timestampMs;
                        _samplesPlayed = 0;
                        _playStarted = true;
                    }

                    while (remaining > 0) {
                        size_t chunk = min((uint32_t)sizeof(audioBuf), remaining);
                        if (!readExact(client, audioBuf, chunk)) {
                            remaining = 0;
                            break;
                        }
                        size_t sent = xStreamBufferSend(_audioStream, audioBuf, chunk, pdMS_TO_TICKS(20));
                        if (sent < chunk) _audioBytesDropped += (chunk - sent);
                        remaining -= chunk;
                    }
                    _audioPackets++;
                    receivedPacket = true;
                } else {
                    if (!discardBytes(client, length)) break;
                }
            }

            client.stop();
            _audioConnected = false;
            _clientConnected = _videoConnected;
        }

        _server.end();
        _netTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void videoNetworkTask() {
        _videoServer.begin();
        _videoServer.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _videoServer.available();
            if (!client) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            client.setNoDelay(true);
            _videoConnected = true;
            _clientConnected = true;
            bool receivedPacket = false;

            while (_isRunning && client.connected()) {
                uint8_t header[16];
                if (!readExact(client, header, sizeof(header), receivedPacket ? 3000 : 8000)) break;

                uint32_t magic = readU32LE(header);
                uint8_t type = header[4];
                uint32_t timestampMs = readU32LE(header + 8);
                uint32_t length = readU32LE(header + 12);

                if (magic != MAGIC || length > 65535) break;

                if (type != PACKET_VIDEO) {
                    if (!discardBytes(client, length)) break;
                    continue;
                }

                if (length > VIDEO_BUFFER_SIZE) {
                    _videoFramesDropped++;
                    if (!discardBytes(client, length)) break;
                    continue;
                }

                uint8_t* buf = nullptr;
                if (xQueueReceive(_emptyQueue, &buf, pdMS_TO_TICKS(5)) != pdTRUE || !buf) {
                    _videoFramesDropped++;
                    if (!discardBytes(client, length)) break;
                    continue;
                }

                if (!readExact(client, buf, length)) {
                    xQueueSend(_emptyQueue, &buf, 0);
                    break;
                }

                VideoFrame frame = { buf, length, timestampMs };
                if (xQueueSend(_videoQueue, &frame, 0) == pdTRUE) {
                    _videoPackets++;
                    receivedPacket = true;
                } else {
                    _videoFramesDropped++;
                    xQueueSend(_emptyQueue, &buf, 0);
                }
            }

            client.stop();
            _videoConnected = false;
            _clientConnected = _audioConnected;
        }

        _videoServer.end();
        _videoTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void audioTask() {
        uint8_t monoBytes[AUDIO_CHUNK_BYTES];
        // 22050 Hz mono doubled to 44100 Hz stereo: each mono sample output as 2 stereo frames (4 samples)
        int16_t stereo[AUDIO_CHUNK_BYTES * 2];

        while (_isRunning) {
            if (!_playReleased) {
                if (_playStarted &&
                    xStreamBufferBytesAvailable(_audioStream) >= START_AUDIO_BYTES &&
                    uxQueueMessagesWaiting(_videoQueue) >= 1) {
                    _samplesPlayed = 0;
                    _playReleased = true;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
            }

            size_t got = xStreamBufferReceive(_audioStream, monoBytes, sizeof(monoBytes), pdMS_TO_TICKS(20));
            uint16_t monoSamples = got / 2;

            if (monoSamples == 0) {
                if (_playReleased && !_audioConnected) {
                    _playReleased = false;
                    _playStarted = false;
                    continue;
                }
                if (_playStarted) {
                    memset(stereo, 0, sizeof(stereo));
                    size_t written = 0;
                    if (poko_tx_handle) {
                        i2s_channel_write(poko_tx_handle, stereo, sizeof(stereo), &written, pdMS_TO_TICKS(20));
                    }
                    _samplesPlayed += AUDIO_CHUNK_BYTES / 2;
                    _audioUnderruns++;
                }
                continue;
            }

            int16_t* mono = (int16_t*)monoBytes;
            float vol = _volume;
            uint16_t outIdx = 0;

            // Upsample 22050 Hz Mono -> 44100 Hz Stereo (each mono sample duplicated into 2 consecutive stereo frames)
            for (uint16_t i = 0; i < monoSamples; i++) {
                int16_t s = (int16_t)(mono[i] * vol);
                // Frame 1
                stereo[outIdx++] = s;
                stereo[outIdx++] = s;
                // Frame 2 (2x oversampling = 44100 Hz)
                stereo[outIdx++] = s;
                stereo[outIdx++] = s;
            }

            size_t written = 0;
            if (poko_tx_handle) {
                i2s_channel_write(poko_tx_handle, stereo, outIdx * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
            }
            _samplesPlayed += monoSamples;
        }

        _audioTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void releaseResources() {
        if (_videoBuffers) {
            for (uint8_t i = 0; i < VIDEO_QUEUE_DEPTH; i++) {
                if (_videoBuffers[i]) heap_caps_free(_videoBuffers[i]);
            }
            free(_videoBuffers);
            _videoBuffers = nullptr;
        }
        if (_videoQueue) {
            vQueueDelete(_videoQueue);
            _videoQueue = NULL;
        }
        if (_emptyQueue) {
            vQueueDelete(_emptyQueue);
            _emptyQueue = NULL;
        }
        if (_audioStream) {
            vStreamBufferDelete(_audioStream);
            _audioStream = NULL;
        }
    }

public:
    SyncedAVPlayer(Arduino_GFX* display, uint16_t port = 1236, float initialVolume = 0.85f)
        : _gfx(display), _port(port), _server(port), _videoServer(port + 1),
          _isRunning(false), _isLoaded(false), _clientConnected(false),
          _audioConnected(false), _videoConnected(false), _wasConnected(false),
          _volume(initialVolume), _allocationFailed(false),
          _netTaskHandle(NULL), _videoTaskHandle(NULL), _audioTaskHandle(NULL),
          _videoQueue(NULL), _emptyQueue(NULL), _audioStream(NULL), _videoBuffers(nullptr),
          _playStarted(false), _playReleased(false), _firstAudioTsMs(0), _samplesPlayed(0),
          _audioPackets(0), _audioBytesDropped(0), _audioUnderruns(0), _videoPackets(0),
          _videoFramesDropped(0), _videoFramesRendered(0), _lastFpsSampleMs(0),
          _lastFpsFrameCount(0), _renderFps(0.0f) {
        _instance = this;
    }

    void setVolume(float vol) {
        _volume = constrain(vol, 0.0f, 1.0f);
    }

    bool isLoaded() const { return _isLoaded; }
    bool isConnected() const { return _clientConnected; }
    bool isPlaying() const { return _playReleased && _clientConnected; }
    float getRenderFps() const { return _renderFps; }

    uint32_t audioClockMs() const {
        if (!_playReleased) return 0;
        return _firstAudioTsMs + (uint32_t)(((uint64_t)_samplesPlayed * 1000ULL) / AUDIO_RATE);
    }

    void load() {
        if (_isLoaded) return;

        TJpgDec.setJpgScale(1);
        TJpgDec.setSwapBytes(true);
        TJpgDec.setCallback(tftOutput);

        _allocationFailed = false;
        _videoBuffers = (uint8_t**)calloc(VIDEO_QUEUE_DEPTH, sizeof(uint8_t*));
        _videoQueue = xQueueCreate(VIDEO_QUEUE_DEPTH, sizeof(VideoFrame));
        _emptyQueue = xQueueCreate(VIDEO_QUEUE_DEPTH, sizeof(uint8_t*));
        _audioStream = xStreamBufferCreate(AUDIO_BUFFER_BYTES, 1);

        if (!_videoBuffers || !_videoQueue || !_emptyQueue || !_audioStream) {
            _allocationFailed = true;
            releaseResources();
            return;
        }

        for (uint8_t i = 0; i < VIDEO_QUEUE_DEPTH; i++) {
            if (psramFound()) {
                _videoBuffers[i] = (uint8_t*)heap_caps_malloc(VIDEO_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!_videoBuffers[i]) {
                _videoBuffers[i] = (uint8_t*)heap_caps_malloc(VIDEO_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (!_videoBuffers[i]) {
                _allocationFailed = true;
                releaseResources();
                return;
            }
            uint8_t* ptr = _videoBuffers[i];
            xQueueSend(_emptyQueue, &ptr, 0);
        }

        resetPlaybackState();
        _isRunning = true;
        _isLoaded = true;
        _clientConnected = false;
        _audioConnected = false;
        _videoConnected = false;
        _wasConnected = false;

        if (xTaskCreatePinnedToCore(audioTaskWrapper, "SyncAudio", 4096, this, 3, &_audioTaskHandle, 0) != pdPASS ||
            xTaskCreatePinnedToCore(networkTaskWrapper, "SyncAudNet", 8192, this, 2, &_netTaskHandle, 0) != pdPASS ||
            xTaskCreatePinnedToCore(videoTaskWrapper, "SyncVidNet", 8192, this, 2, &_videoTaskHandle, 0) != pdPASS) {
            _isRunning = false;
            unload();
            _allocationFailed = true;
            return;
        }
    }

    void unload() {
        if (!_isLoaded && !_allocationFailed) return;

        _isRunning = false;
        uint32_t deadline = millis() + 1200;
        while (((_netTaskHandle != NULL) || (_videoTaskHandle != NULL) || (_audioTaskHandle != NULL)) && millis() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        if (_netTaskHandle != NULL) {
            _server.end();
            vTaskDelete(_netTaskHandle);
            _netTaskHandle = NULL;
        }
        if (_videoTaskHandle != NULL) {
            _videoServer.end();
            vTaskDelete(_videoTaskHandle);
            _videoTaskHandle = NULL;
        }
        if (_audioTaskHandle != NULL) {
            vTaskDelete(_audioTaskHandle);
            _audioTaskHandle = NULL;
        }

        releaseResources();
        _isLoaded = false;
        _clientConnected = false;
        _audioConnected = false;
        _videoConnected = false;
    }

    void update() {
        if (!_isLoaded) return;

        if (!_wasConnected && _clientConnected) {
            _wasConnected = true;
            if (pokoGfx) pokoGfx->fillScreen(RGB565_BLACK);
        } else if (_wasConnected && !_clientConnected) {
            _wasConnected = false;
        }

        if (!_playReleased) return;

        uint32_t clockMs = audioClockMs();
        VideoFrame f;

        while (xQueuePeek(_videoQueue, &f, 0) == pdTRUE) {
            if (f.timestampMs + VIDEO_LATE_DROP_MS < clockMs) {
                xQueueReceive(_videoQueue, &f, 0);
                _videoFramesDropped++;
                uint8_t* ptr = f.buffer;
                xQueueSend(_emptyQueue, &ptr, 0);
                continue;
            }

            if ((int32_t)f.timestampMs <= (int32_t)clockMs + VIDEO_EARLY_MS) {
                xQueueReceive(_videoQueue, &f, 0);
                // Draw full-screen 128×128 frame directly onto the display
                TJpgDec.drawJpg(0, 0, f.buffer, f.length);
                _videoFramesRendered++;
                uint8_t* ptr = f.buffer;
                xQueueSend(_emptyQueue, &ptr, 0);
            }
            break;
        }

        uint32_t now = millis();
        if (now - _lastFpsSampleMs >= 1000) {
            uint32_t deltaFrames = _videoFramesRendered - _lastFpsFrameCount;
            uint32_t deltaMs = now - _lastFpsSampleMs;
            if (deltaMs > 0) _renderFps = (deltaFrames * 1000.0f) / deltaMs;
            _lastFpsFrameCount = _videoFramesRendered;
            _lastFpsSampleMs = now;
        }
    }
};

inline SyncedAVPlayer* SyncedAVPlayer::_instance = nullptr;
