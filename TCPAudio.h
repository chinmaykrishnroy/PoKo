#pragma once
#include <WiFi.h>
#include <driver/i2s_std.h>
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include "PokoDrivers.h"

// ─────────────────────────────────────────────────────────────
//  TCPAudio — MP3 Audio Stream Receiver (Port 1235)
//  Receives raw MP3 stream over TCP from backend server, decodes
//  using ESP8266Audio / AudioGeneratorMP3, and outputs directly
//  to the ES8311 DAC via poko_tx_handle (I2S).
// ─────────────────────────────────────────────────────────────

class TCPAudio {
private:
    uint16_t        _port;
    WiFiServer      _server;

    bool            _isLoaded;
    volatile bool   _isRunning;
    volatile TaskHandle_t _netTaskHandle;

    volatile bool   _clientConnected;
    volatile bool   _abortStream;
    volatile bool   _playStarted;
    volatile uint32_t _disconnectStartMs;
    volatile float  _volume;
    WiFiClient* volatile _activeClient = nullptr;

    class AudioStreamTCP : public AudioFileSource {
    private:
        WiFiClient*     _client;
        volatile bool*  _isRunning;
        volatile bool*  _abort;
        uint8_t*        _ring;
        size_t          _capacity;
        size_t          _head;
        size_t          _tail;
        size_t          _count;
        bool            _prebuffered;

        void pump() {
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

    public:
        AudioStreamTCP(WiFiClient* client, volatile bool* isRunning, volatile bool* abortFlag, size_t bufferBytes = 131072)
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

        virtual ~AudioStreamTCP() override {
            close();
        }

        virtual uint32_t read(void *data, uint32_t len) override {
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
            size_t toCopy = min((size_t)len, _count);
            size_t copied = 0;
            while (copied < toCopy) {
                size_t chunk = min(toCopy - copied, _capacity - _tail);
                memcpy(((uint8_t*)data) + copied, _ring + _tail, chunk);
                _tail = (_tail + chunk) % _capacity;
                copied += chunk;
            }
            _count -= copied;

            pump();
            return (uint32_t)copied;
        }

        virtual uint32_t readNonBlock(void *data, uint32_t len) override { return read(data, len); }
        virtual bool seek(int32_t pos, int dir) override { return false; }
        virtual bool close() override {
            if (_ring) {
                if (psramFound()) heap_caps_free(_ring);
                else free(_ring);
                _ring = nullptr;
            }
            _capacity = 0;
            _count = 0;
            return true;
        }
        virtual bool isOpen() override { return _client && (_client->connected() || _count > 0); }
        virtual uint32_t getSize() override { return 0; }
        virtual uint32_t getPos() override { return 0; }
    };

    class AudioOutputPokoI2S : public AudioOutput {
    private:
        volatile float* _vol;
        uint32_t        _sampleCount = 0;
        int16_t         _buffer[512];
        int             _bufIndex = 0;
    public:
        AudioOutputPokoI2S(volatile float* vol) : _vol(vol) {}

        virtual bool begin() override {
            _bufIndex = 0;
            _sampleCount = 0;
            return true;
        }
        virtual bool SetRate(int hz) override {
            if (hz > 0) {
                ensureAudioOutput((uint32_t)hz);
            }
            return true;
        }
        virtual bool SetChannels(int channels) override { return true; }

        virtual bool ConsumeSample(int16_t sample[2]) override {
            float v = *_vol;
            // Downsample audio reactivity to 1-in-8 samples (~5.5 kHz) for 87% lower CPU load
            if ((_sampleCount++ & 0x07) == 0) {
                pixelEngine.feedAudioSample(sample[0], sample[1]);
            }
            _buffer[_bufIndex++] = (int16_t)(sample[0] * v);
            _buffer[_bufIndex++] = (int16_t)(sample[1] * v);

            if (_bufIndex >= 512) {
                size_t written = 0;
                if (!poko_tx_handle) {
                    ensureAudioOutput(44100);
                }
                if (poko_tx_handle) {
                    i2s_channel_write(poko_tx_handle, _buffer, sizeof(_buffer), &written, pdMS_TO_TICKS(100));
                }
                _bufIndex = 0;
            }
            return true;
        }

        virtual bool stop() override {
            if (_bufIndex > 0) {
                size_t written = 0;
                if (poko_tx_handle) {
                    i2s_channel_write(poko_tx_handle, _buffer, _bufIndex * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
                }
                _bufIndex = 0;
            }
            return true;
        }
    };

    static void networkTaskWrapper(void* pvParameters) {
        ((TCPAudio*)pvParameters)->networkTask();
    }

    void networkTask() {
        ensureAudioOutput(44100);
        _server.begin();
        _server.setNoDelay(true);

        while (_isRunning) {
            WiFiClient client = _server.available();
            if (client) {
                Serial.println("[tcpaudio] Client connected, starting MP3 stream decode");
                ensureAudioOutput(44100);
                _activeClient = &client;
                _clientConnected = true;
                _playStarted = true;
                _disconnectStartMs = 0;
                _abortStream = false;
                client.setNoDelay(true);

                AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
                AudioOutputPokoI2S* out = new AudioOutputPokoI2S(&_volume);
                AudioStreamTCP* file   = new AudioStreamTCP(&client, &_isRunning, &_abortStream);

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
                _activeClient = nullptr;
                _clientConnected = false;
                _abortStream = false;
                Serial.println("[tcpaudio] Client disconnected");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        _server.end();
        _netTaskHandle = NULL;
        vTaskDelete(NULL);
    }

public:
    TCPAudio(uint16_t port = 1235, float initialVolume = 1.0f)
        : _port(port), _server(port), _isLoaded(false), _isRunning(false),
          _netTaskHandle(NULL), _clientConnected(false), _abortStream(false),
          _playStarted(false), _disconnectStartMs(0), _volume(initialVolume) {}

    void setVolume(float vol) {
        _volume = constrain(vol, 0.0f, 1.0f);
    }

    bool isLoaded() const { return _isLoaded; }
    bool isConnected() const { return _clientConnected; }

    void stopStream() {
        if (_clientConnected) {
            _abortStream = true;
            if (_activeClient && _activeClient->connected()) {
                _activeClient->stop();
            }
            uint32_t startWait = millis();
            while (_clientConnected && millis() - startWait < 300) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        _playStarted = false;
        _disconnectStartMs = 0;
    }

    bool hasFinished() {
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

    void reset() {
        stopStream();
    }

    void load() {
        if (!_isLoaded) {
            ensureAudioOutput(44100);

            while (_netTaskHandle != NULL) {
                vTaskDelay(pdMS_TO_TICKS(2));
            }

            _isRunning = true;
            _clientConnected = false;
            _isLoaded = true;

            BaseType_t ok = xTaskCreatePinnedToCore(
                networkTaskWrapper, "PokoAudNet", 16384, this, 2, (TaskHandle_t*)&_netTaskHandle, 0
            );
            if (ok != pdPASS) {
                _isRunning = false;
                _isLoaded = false;
                _netTaskHandle = NULL;
                return;
            }
        }
    }

    void unload() {
        if (_isLoaded) {
            _isRunning = false;
            stopStream();
            _server.end();
            _isLoaded = false;
            uint32_t deadline = millis() + 2000;
            while (_netTaskHandle != NULL && millis() < deadline) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (_netTaskHandle != NULL) {
                Serial.println("[tcpaudio] Warning: task shutdown timeout");
            }
            _clientConnected = false;
        }
    }
};
