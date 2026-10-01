#include "SyncedAVPlayer.h"

SyncedAVPlayer* SyncedAVPlayer::_instance = nullptr;

bool SyncedAVPlayer::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
        if (!_instance || !_instance->_isLoaded) return false;
        if (pokoGfx) {
            pokoGfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
        }
        return true;
    }

void SyncedAVPlayer::networkTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->audioNetworkTask();
    }

void SyncedAVPlayer::videoTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->videoNetworkTask();
    }

void SyncedAVPlayer::audioTaskWrapper(void* arg) {
        ((SyncedAVPlayer*)arg)->audioTask();
    }

uint32_t SyncedAVPlayer::readU32LE(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

bool SyncedAVPlayer::readExact(WiFiClient& client, uint8_t* dst, size_t len, uint32_t idleTimeoutMs) {
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

bool SyncedAVPlayer::discardBytes(WiFiClient& client, uint32_t len) {
        uint8_t scratch[512];
        while (len > 0 && _isRunning && client.connected()) {
            size_t chunk = min((uint32_t)sizeof(scratch), len);
            if (!readExact(client, scratch, chunk)) return false;
            len -= chunk;
        }
        return true;
    }

void SyncedAVPlayer::resetPlaybackState() {
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
        _disconnectStartMs = 0;
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

void SyncedAVPlayer::audioNetworkTask() {
        _server.begin();
        _server.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _server.available();
            if (!client) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            client.setNoDelay(true);
            _activeAudioClient = &client;
            _audioConnected = true;
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
            _activeAudioClient = nullptr;
            _audioConnected = false;
            _clientConnected = _videoConnected;
        }

        _server.end();
        _netTaskHandle = NULL;
        if (_netTaskDone) xSemaphoreGive(_netTaskDone);
        vTaskDelete(NULL);
    }

void SyncedAVPlayer::videoNetworkTask() {
        _videoServer.begin();
        _videoServer.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _videoServer.available();
            if (!client) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            client.setNoDelay(true);
            _activeVideoClient = &client;
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
                if (xQueueReceive(_emptyQueue, &buf, pdMS_TO_TICKS(100)) != pdTRUE || !buf) {
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
            _activeVideoClient = nullptr;
            _videoConnected = false;
            _clientConnected = _audioConnected;
        }

        _videoServer.end();
        _videoTaskHandle = NULL;
        if (_videoTaskDone) xSemaphoreGive(_videoTaskDone);
        vTaskDelete(NULL);
    }

void SyncedAVPlayer::audioTask() {
        uint8_t monoBytes[AUDIO_CHUNK_BYTES];
        // 22050 Hz mono doubled to 44100 Hz stereo: each mono sample output as 2 stereo frames (4 samples)
        int16_t stereo[AUDIO_CHUNK_BYTES * 2];

        while (_isRunning) {
            if (!_playReleased) {
                if (_audioConnected) {
                    if (_playStarted && xStreamBufferBytesAvailable(_audioStream) >= START_AUDIO_BYTES &&
                        (!_videoConnected || uxQueueMessagesWaiting(_videoQueue) >= 1)) {
                        _samplesPlayed = 0;
                        _wallClockStartMs = millis();
                        _playReleased = true;
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(5));
                        continue;
                    }
                } else if (_videoConnected && uxQueueMessagesWaiting(_videoQueue) >= 2) {
                    _samplesPlayed = 0;
                    _wallClockStartMs = millis();
                    _playReleased = true;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
            }

            size_t got = xStreamBufferReceive(_audioStream, monoBytes, sizeof(monoBytes), pdMS_TO_TICKS(20));
            uint16_t monoSamples = got / 2;

            if (monoSamples == 0) {
                // Stop playback only if BOTH transports are gone.
                if (_playReleased && !_audioConnected && !_videoConnected) {
                    // Keep the clock released so update() can drain queued
                    // video and hasFinished() can observe end-of-stream.
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }

                // If video is still connected but audio isn't, keep _playReleased = true
                // and audioClockMs() will automatically use wall clock.
                if (_playStarted && _audioConnected) {
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
            if (!_isRunning) break;
            if (!poko_tx_handle) {
                ensureAudioOutput(44100);
            }
            if (poko_tx_handle && _isRunning) {
                i2s_channel_write(poko_tx_handle, stereo, outIdx * sizeof(int16_t), &written, pdMS_TO_TICKS(30));
            }
            _samplesPlayed += monoSamples;
        }

        _audioTaskHandle = NULL;
        if (_audioTaskDone) xSemaphoreGive(_audioTaskDone);
        vTaskDelete(NULL);
    }

void SyncedAVPlayer::releaseResources() {
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

SyncedAVPlayer::SyncedAVPlayer(Arduino_GFX* display, uint16_t port, float initialVolume)
        : _gfx(display), _port(port), _server(port), _videoServer(port + 1),
          _isRunning(false), _isLoaded(false), _clientConnected(false),
          _audioConnected(false), _videoConnected(false), _wasConnected(false),
          _volume(initialVolume), _allocationFailed(false), _disconnectStartMs(0),
          _netTaskHandle(NULL), _videoTaskHandle(NULL), _audioTaskHandle(NULL),
          _videoQueue(NULL), _emptyQueue(NULL), _audioStream(NULL), _videoBuffers(nullptr),
          _playStarted(false), _playReleased(false), _firstAudioTsMs(0), _samplesPlayed(0),
          _wallClockStartMs(0),
          _audioPackets(0), _audioBytesDropped(0), _audioUnderruns(0), _videoPackets(0),
          _videoFramesDropped(0), _videoFramesRendered(0), _lastFpsSampleMs(0),
          _lastFpsFrameCount(0), _renderFps(0.0f) {
        _instance = this;
    }

void SyncedAVPlayer::setVolume(float vol) {
        _volume = constrain(vol, 0.0f, 1.0f);
    }

bool SyncedAVPlayer::isLoaded() const { return _isLoaded; }

bool SyncedAVPlayer::isRunning() const { return _isRunning; }

bool SyncedAVPlayer::hasStarted() const { return _playReleased; }

bool SyncedAVPlayer::isConnected() const { return _clientConnected; }

bool SyncedAVPlayer::isPlaying() const { return _playReleased && _clientConnected; }

float SyncedAVPlayer::getRenderFps() const { return _renderFps; }

uint32_t SyncedAVPlayer::audioClockMs() const {
        if (!_playReleased) return 0;
        if (_audioConnected && _playStarted && _samplesPlayed > 0) {
            return _firstAudioTsMs + (uint32_t)(((uint64_t)_samplesPlayed * 1000ULL) / AUDIO_RATE);
        }
        return _firstAudioTsMs + (millis() - _wallClockStartMs);
    }

bool SyncedAVPlayer::hasFinished() {
        if (!_playReleased || _clientConnected) {
            _disconnectStartMs = 0;
            return false;
        }
        if (_videoQueue && uxQueueMessagesWaiting(_videoQueue) > 0) {
            _disconnectStartMs = 0;
            return false;
        }
        if (_disconnectStartMs == 0) {
            _disconnectStartMs = millis();
            return false;
        }
        return (millis() - _disconnectStartMs > 800);
    }

void SyncedAVPlayer::reset() {
        // Public resets must never clear buffers while producers still use them.
        bool reload = _isLoaded;
        if (reload) unload();
        if (_isLoaded) return; // timed-out shutdown: preserve worker resources
        if (reload) load();
        else resetPlaybackState();
    }

void SyncedAVPlayer::load() {
        if (_isLoaded && _isRunning) return;

        if (_isLoaded && !_isRunning) {
            if (_audioTaskHandle == NULL && _netTaskHandle == NULL && _videoTaskHandle == NULL) {
                releaseResources();
                _isLoaded = false;
            } else {
                if (_audioTaskDone && _audioTaskHandle != NULL) xSemaphoreTake(_audioTaskDone, pdMS_TO_TICKS(100));
                if (_netTaskDone && _netTaskHandle != NULL) xSemaphoreTake(_netTaskDone, pdMS_TO_TICKS(100));
                if (_videoTaskDone && _videoTaskHandle != NULL) xSemaphoreTake(_videoTaskDone, pdMS_TO_TICKS(100));
                if (_audioTaskHandle == NULL && _netTaskHandle == NULL && _videoTaskHandle == NULL) {
                    releaseResources();
                    _isLoaded = false;
                } else {
                    Serial.println("[synced] Warning: worker tasks still active, cannot load");
                    return;
                }
            }
        }

        if (_audioTaskHandle != NULL || _netTaskHandle != NULL || _videoTaskHandle != NULL) {
            Serial.println("[synced] Warning: worker tasks still active, cannot load");
            return;
        }

        ensureAudioOutput(44100);

        TJpgDec.setJpgScale(1);
        TJpgDec.setSwapBytes(false);
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

        if (!_netTaskDone) _netTaskDone = xSemaphoreCreateBinary();
        if (!_videoTaskDone) _videoTaskDone = xSemaphoreCreateBinary();
        if (!_audioTaskDone) _audioTaskDone = xSemaphoreCreateBinary();
        if (!_netTaskDone || !_videoTaskDone || !_audioTaskDone) {
            Serial.println("[synced] Error: could not allocate worker completion semaphore(s)");
            _allocationFailed = true;
            _isRunning = false;
            _isLoaded = false;
            releaseResources();
            return;
        }
        xSemaphoreTake(_netTaskDone, 0);
        xSemaphoreTake(_videoTaskDone, 0);
        xSemaphoreTake(_audioTaskDone, 0);

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

void SyncedAVPlayer::unload() {
        if (!_isLoaded && !_allocationFailed) return;

        _isRunning = false;

        bool taskTimeout = false;
        // Give tasks up to 400ms each to cleanly exit their loops and delete themselves
        if (_audioTaskDone && _audioTaskHandle != NULL) {
            if (xSemaphoreTake(_audioTaskDone, pdMS_TO_TICKS(400)) != pdTRUE) taskTimeout = true;
        }
        if (_netTaskDone && _netTaskHandle != NULL) {
            if (xSemaphoreTake(_netTaskDone, pdMS_TO_TICKS(400)) != pdTRUE) taskTimeout = true;
        }
        if (_videoTaskDone && _videoTaskHandle != NULL) {
            if (xSemaphoreTake(_videoTaskDone, pdMS_TO_TICKS(400)) != pdTRUE) taskTimeout = true;
        }

        if (taskTimeout) {
            Serial.println("[synced] Warning: task shutdown timeout, preserving buffers to avoid use-after-free");
            return;
        }

        _audioTaskHandle = NULL;
        _netTaskHandle = NULL;
        _videoTaskHandle = NULL;
        _activeAudioClient = nullptr;
        _activeVideoClient = nullptr;

        vTaskDelay(pdMS_TO_TICKS(25)); // Allow Core 0 idle task to reclaim FreeRTOS task stacks

        releaseResources();
        _isLoaded = false;
        _clientConnected = false;
        _audioConnected = false;
        _videoConnected = false;
    }

void SyncedAVPlayer::update() {
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
                TJpgDec.setSwapBytes(false);
                TJpgDec.setCallback(tftOutput);
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
