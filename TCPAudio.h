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
    volatile float  _volume;

    class AudioStreamTCP : public AudioFileSource {
    private:
        WiFiClient*     _client;
        volatile bool*  _isRunning;
    public:
        AudioStreamTCP(WiFiClient* client, volatile bool* isRunning)
            : _client(client), _isRunning(isRunning) {}

        virtual uint32_t read(void *data, uint32_t len) override {
            if (!_client || !_client->connected()) return 0;
            uint32_t readBytes = 0;
            unsigned long startWait = millis();

            while (readBytes < len && _client->connected() && *_isRunning) {
                int avail = _client->available();
                if (avail > 0) {
                    int toRead = min((size_t)avail, (size_t)(len - readBytes));
                    int r = _client->read(((uint8_t*)data) + readBytes, toRead);
                    if (r > 0) {
                        readBytes += r;
                        startWait = millis();
                    }
                } else {
                    if (readBytes > 0) break;
                    if (millis() - startWait > 3500) break;
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            return readBytes;
        }

        virtual uint32_t readNonBlock(void *data, uint32_t len) override { return read(data, len); }
        virtual bool seek(int32_t pos, int dir) override { return false; }
        virtual bool close() override { return true; }
        virtual bool isOpen() override { return _client && _client->connected(); }
        virtual uint32_t getSize() override { return 0; }
        virtual uint32_t getPos() override { return 0; }
    };

    class AudioOutputPokoI2S : public AudioOutput {
    private:
        volatile float* _vol;
        int16_t         _buffer[512];
        int             _bufIndex = 0;
    public:
        AudioOutputPokoI2S(volatile float* vol) : _vol(vol) {}

        virtual bool begin() override {
            _bufIndex = 0;
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
                _clientConnected = true;
                client.setNoDelay(true);

                AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
                AudioOutputPokoI2S* out = new AudioOutputPokoI2S(&_volume);
                AudioStreamTCP* file   = new AudioStreamTCP(&client, &_isRunning);

                if (mp3->begin(file, out)) {
                    Serial.println("[tcpaudio] MP3 begin OK, streaming...");
                    while (client.connected() && _isRunning && mp3->isRunning()) {
                        if (!mp3->loop()) {
                            Serial.println("[tcpaudio] MP3 stream ended");
                            mp3->stop();
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(1));
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
          _netTaskHandle(NULL), _clientConnected(false), _volume(initialVolume) {}

    void setVolume(float vol) {
        _volume = constrain(vol, 0.0f, 1.0f);
    }

    bool isLoaded() const { return _isLoaded; }
    bool isConnected() const { return _clientConnected; }

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
                networkTaskWrapper, "PokoAudNet", 8192, this, 2, (TaskHandle_t*)&_netTaskHandle, 0
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
            _isLoaded = false;
            uint32_t deadline = millis() + 1500;
            while (_netTaskHandle != NULL && millis() < deadline) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (_netTaskHandle != NULL) {
                _server.end();
                vTaskDelete((TaskHandle_t)_netTaskHandle);
                _netTaskHandle = NULL;
            }
            _clientConnected = false;
        }
    }
};
