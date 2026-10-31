#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// Passive, bounded PCM observer. Producers never wait for the UI to consume
// samples, allocate memory, or run DSP. It does not own/configure I2S or the PA.
class PlaybackAudioTap {
public:
    void enable(bool enabled);
    bool enabled() const;
    void writeStereo(const int16_t* pcm, size_t frames, uint32_t sampleRate, uint32_t nowMs = 0);
    size_t read(int16_t* mono, size_t frames, uint32_t& rate, uint32_t& generation);
    uint32_t overruns();
private:
    static constexpr size_t CAPACITY = 8192;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<bool> _enabled{false};
    int16_t _samples[CAPACITY];
    size_t _head = 0, _tail = 0, _count = 0;
    uint32_t _rate = 0, _generation = 0, _overruns = 0, _silentFrames = 0, _lastWriteMs = 0;
    int _channel = -1;
};

extern PlaybackAudioTap playbackAudioTap;
