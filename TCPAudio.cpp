#include "TCPAudio.h"
#include <new>

void TCPAudio::networkTaskWrapper(void* pvParameters) {
        ((TCPAudio*)pvParameters)->networkTask();
    }

void TCPAudio::networkTask() {
        ensureAudioOutput(44100);
        _server.begin();
        _server.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _server.available();
            if (client) {
                Serial.println("[tcpaudio] Client connected, starting MP3 stream decode");
                ensureAudioOutput(44100);
                _clientConnected = true;
                _playStarted = true;
                _disconnectStartMs = 0;
                _abortStream = false;
                client.setNoDelay(true);

                AudioGeneratorMP3* mp3 = new (std::nothrow) AudioGeneratorMP3();
                AudioOutputPokoI2S* out = new (std::nothrow) AudioOutputPokoI2S(&_volume);
                AudioStreamTCP* file   = new (std::nothrow) AudioStreamTCP(&client, &_isRunning, &_abortStream);

                if (!mp3 || !out || !file || !file->isOpen()) {
                    Serial.println("[tcpaudio] Insufficient memory for MP3 stream");
                    delete mp3;
                    delete out;
                    delete file;
                    client.stop();
                    _clientConnected = false;
                    _abortStream = false;
                    vTaskDelay(pdMS_TO_TICKS(25));
                    continue;
                }

                if (mp3->begin(file, out)) {
                    Serial.println("[tcpaudio] MP3 begin OK, streaming...");
                    uint32_t frameCount = 0;
                    while (client.connected() && _isRunning && !_abortStream && mp3->isRunning()) {
                        if (!mp3->loop()) {
                            Serial.println("[tcpaudio] MP3 stream ended");
                            mp3->stop();
                            break;
                        }
                        if ((++frameCount & 0x07) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(1));
                        } else {
                            taskYIELD();
                        }
                    }
                } else {
                    Serial.println("[tcpaudio] MP3 begin FAILED");
                }

                if (mp3->isRunning()) mp3->stop();

                delete mp3;
                delete out;
                delete file;

                client.stop();
                _clientConnected = false;
                _abortStream = false;
                Serial.println("[tcpaudio] Client disconnected");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        _server.end();
        if (_netTaskDone) xSemaphoreGive(_netTaskDone);
        vTaskDelete(NULL);
    }

TCPAudio::TCPAudio(uint16_t port, float initialVolume)
        : _port(port), _server(port), _isLoaded(false), _isRunning(false),
          _netTaskHandle(NULL), _netTaskDone(nullptr), _clientConnected(false), _abortStream(false),
          _playStarted(false), _disconnectStartMs(0), _volume(initialVolume) {}

void TCPAudio::setVolume(float vol) {
        _volume = constrain(vol, 0.0f, 1.0f);
    }

bool TCPAudio::isLoaded() const { return _isLoaded; }

bool TCPAudio::isConnected() const { return _clientConnected.load(std::memory_order_acquire); }

void TCPAudio::stopStream() {
        if (_clientConnected) {
            _abortStream = true;
            uint32_t startWait = millis();
            while (_clientConnected && millis() - startWait < 300) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        _playStarted = false;
        _disconnectStartMs = 0;
    }

bool TCPAudio::hasFinished() {
        if (!_playStarted || _clientConnected) {
            _disconnectStartMs = 0;
            return false;
        }
        if (_disconnectStartMs == 0) {
            _disconnectStartMs = millis();
            return false;
        }
        return (millis() - _disconnectStartMs > 600);
    }

void TCPAudio::reset() {
        stopStream();
    }

void TCPAudio::load() {
        if (!_isLoaded) {
            if (_netTaskHandle != NULL) {
                Serial.println("[tcpaudio] Warning: previous task still running, cannot load");
                return;
            }
            ensureAudioOutput(44100);

            if (!_netTaskDone) {
                _netTaskDone = xSemaphoreCreateBinary();
            }
            if (!_netTaskDone) {
                Serial.println("[tcpaudio] Error: could not allocate task completion semaphore");
                _isRunning = false;
                _isLoaded = false;
                return;
            }
            xSemaphoreTake(_netTaskDone, 0);

            _isRunning = true;
            _clientConnected = false;
            _isLoaded = true;

            BaseType_t ok = xTaskCreatePinnedToCore(
                networkTaskWrapper, "PokoAudNet", 16384, this, 2, &_netTaskHandle, 0
            );
            if (ok != pdPASS) {
                _isRunning = false;
                _isLoaded = false;
                _netTaskHandle = NULL;
                return;
            }
        }
    }

void TCPAudio::unload() {
        if (_isLoaded) {
            _isRunning = false;
            stopStream();
            if (_netTaskHandle != NULL) {
                if (!_netTaskDone) {
                    Serial.println("[tcpaudio] Error: cannot verify task shutdown; keeping player loaded");
                    return;
                }
                if (xSemaphoreTake(_netTaskDone, pdMS_TO_TICKS(1500)) != pdTRUE) {
                    Serial.println("[tcpaudio] Error: task shutdown timeout; keeping player loaded");
                    return;
                }
            }
            _netTaskHandle = NULL;
            _clientConnected = false;
            _isLoaded = false;
        }
    }

uint32_t TCPAudio::AudioStreamTCP::readBuffered(void* data, uint32_t len) {
            size_t toCopy = min((size_t)len, _count);
            size_t copied = 0;
            while (copied < toCopy) {
                size_t chunk = min(toCopy - copied, _capacity - _tail);
                memcpy(((uint8_t*)data) + copied, _ring + _tail, chunk);
                _tail = (_tail + chunk) % _capacity;
                copied += chunk;
            }
            _count -= copied;
            return (uint32_t)copied;
        }

void TCPAudio::AudioStreamTCP::pump() {
            if (!_client || !_client->connected() || (*_abort)) return;
            while (_client->available() > 0 && _count < _capacity) {
                size_t spaceToEnd = _capacity - _head;
                size_t canRead = min((size_t)_client->available(), _capacity - _count);
                canRead = min(canRead, spaceToEnd);
                if (canRead == 0) break;
                int n = _client->read(_ring + _head, canRead);
                if (n > 0) {
                    _head = (_head + n) % _capacity;
                    _count += n;
                } else {
                    break;
                }
            }
        }

TCPAudio::AudioStreamTCP::AudioStreamTCP(WiFiClient* client, std::atomic<bool>* isRunning, std::atomic<bool>* abortFlag, size_t bufferBytes)
            : _client(client), _isRunning(isRunning), _abort(abortFlag), _capacity(bufferBytes),
              _head(0), _tail(0), _count(0), _prebuffered(false), _ring(nullptr) {
            if (psramFound()) {
                _ring = (uint8_t*)heap_caps_malloc(_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!_ring) {
                _ring = (uint8_t*)malloc(_capacity);
            }
            if (!_ring) {
                _capacity = 0;
            }
        }

TCPAudio::AudioStreamTCP::~AudioStreamTCP() {
            close();
        }

uint32_t TCPAudio::AudioStreamTCP::read(void *data, uint32_t len) {
            if (!_ring || _capacity == 0 || (*_abort)) return 0;
            if (!_client || (!_client->connected() && _count == 0)) return 0;

            // Initial pre-buffering (~1.5s of audio @ 128kbps) to absorb network jitter
            if (!_prebuffered) {
                uint32_t preStart = millis();
                while (_client && _client->connected() && *_isRunning && !(*_abort) && _count < 24576) {
                    pump();
                    if (_count >= 24576 || millis() - preStart > 1500) break;
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                _prebuffered = true;
            }

            // Pump latest TCP packets into ring
            pump();

            // Wait briefly if ring buffer is empty but client is still streaming
            if (_count == 0 && _client && _client->connected() && *_isRunning && !(*_abort)) {
                uint32_t waitStart = millis();
                while (_count == 0 && _client->connected() && *_isRunning && !(*_abort)) {
                    pump();
                    if (_count > 0 || millis() - waitStart > 200) break;
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }

            if (_count == 0 || (*_abort)) return 0;

            // Read from ring buffer into caller data
            uint32_t copied = readBuffered(data, len);
            pump();
            return copied;
        }

uint32_t TCPAudio::AudioStreamTCP::readNonBlock(void *data, uint32_t len) {
            if (!_ring || _capacity == 0 || (*_abort)) return 0;
            pump();
            return readBuffered(data, len);
        }

bool TCPAudio::AudioStreamTCP::seek(int32_t pos, int dir) { return false; }

bool TCPAudio::AudioStreamTCP::close() {
            if (_ring) {
                if (psramFound()) heap_caps_free(_ring);
                else free(_ring);
                _ring = nullptr;
            }
            _capacity = 0;
            _count = 0;
            return true;
        }

bool TCPAudio::AudioStreamTCP::isOpen() {
            return _ring && _capacity > 0 && _client && (_client->connected() || _count > 0);
        }

uint32_t TCPAudio::AudioStreamTCP::getSize() { return 0; }

uint32_t TCPAudio::AudioStreamTCP::getPos() { return 0; }

TCPAudio::AudioOutputPokoI2S::AudioOutputPokoI2S(std::atomic<float>* vol) : _vol(vol) {}

bool TCPAudio::AudioOutputPokoI2S::begin() {
            _bufIndex = 0;
            _sampleCount = 0;
            return true;
        }

bool TCPAudio::AudioOutputPokoI2S::SetRate(int hz) {
            if (hz > 0) {
                ensureAudioOutput((uint32_t)hz);
            }
            return true;
        }

bool TCPAudio::AudioOutputPokoI2S::SetChannels(int channels) { return true; }

bool TCPAudio::AudioOutputPokoI2S::ConsumeSample(int16_t sample[2]) {
            float v = _vol->load(std::memory_order_relaxed);
            // Downsample audio reactivity to 1-in-8 samples (~5.5 kHz) for 87% lower CPU load
            if ((_sampleCount++ & 0x07) == 0) {
                pixelEngine.feedAudioSample(sample[0], sample[1]);
            }
            _buffer[_bufIndex++] = (int16_t)(sample[0] * v);
            _buffer[_bufIndex++] = (int16_t)(sample[1] * v);

            if (_bufIndex >= 512) {
                size_t written = 0;
                if (poko_tx_handle) {
                    i2s_channel_write(poko_tx_handle, _buffer, sizeof(_buffer), &written, pdMS_TO_TICKS(100));
                }
                _bufIndex = 0;
            }
            return true;
        }

bool TCPAudio::AudioOutputPokoI2S::stop() {
            if (_bufIndex > 0) {
                size_t written = 0;
                if (poko_tx_handle) {
                    i2s_channel_write(poko_tx_handle, _buffer, _bufIndex * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
                }
                _bufIndex = 0;
            }
            return true;
        }
