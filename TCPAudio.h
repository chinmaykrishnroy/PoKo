#pragma once
#include <WiFi.h>
#include <driver/i2s_std.h>
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <atomic>
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
    std::atomic<bool> _isRunning{false};
    TaskHandle_t      _netTaskHandle = nullptr; // owned by the Arduino loop task
    SemaphoreHandle_t     _netTaskDone = nullptr;

    std::atomic<bool>     _clientConnected{false};
    std::atomic<bool>     _abortStream{false};
    std::atomic<bool>     _playStarted{false};
    std::atomic<uint32_t> _disconnectStartMs{0};
    std::atomic<float>    _volume{1.0f};

    class AudioStreamTCP : public AudioFileSource {
    private:
        WiFiClient*     _client;
        std::atomic<bool>* _isRunning;
        std::atomic<bool>* _abort;
        uint8_t*        _ring;
        size_t          _capacity;
        size_t          _head;
        size_t          _tail;
        size_t          _count;
        bool            _prebuffered;

        uint32_t readBuffered(void* data, uint32_t len);

        void pump();

    public:
        AudioStreamTCP(WiFiClient* client, std::atomic<bool>* isRunning, std::atomic<bool>* abortFlag, size_t bufferBytes = 131072);

        virtual ~AudioStreamTCP() override;

        virtual uint32_t read(void *data, uint32_t len) override;

        virtual uint32_t readNonBlock(void *data, uint32_t len) override;
        virtual bool seek(int32_t pos, int dir) override;
        virtual bool close() override;
        virtual bool isOpen() override;
        virtual uint32_t getSize() override;
        virtual uint32_t getPos() override;
    };

    class AudioOutputPokoI2S : public AudioOutput {
    private:
        std::atomic<float>* _vol;
        uint32_t        _sampleCount = 0;
        int16_t         _buffer[512];
        int             _bufIndex = 0;
    public:
        explicit AudioOutputPokoI2S(std::atomic<float>* vol);

        virtual bool begin() override;
        virtual bool SetRate(int hz) override;
        virtual bool SetChannels(int channels) override;

        virtual bool ConsumeSample(int16_t sample[2]) override;

        virtual bool stop() override;
    };

    static void networkTaskWrapper(void* pvParameters);

    void networkTask();

public:
    TCPAudio(uint16_t port = 1235, float initialVolume = 1.0f);

    void setVolume(float vol);

    bool isLoaded() const;
    bool isConnected() const;

    void stopStream();

    bool hasFinished();

    void reset();

    void load();

    void unload();
};

