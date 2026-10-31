#include "PlaybackAudioTap.h"
#include <algorithm>
#include <stdlib.h>

PlaybackAudioTap playbackAudioTap;

void PlaybackAudioTap::enable(bool enabled) {
    portENTER_CRITICAL(&_mux);
    _enabled.store(enabled, std::memory_order_relaxed);
    _head = _tail = _count = 0;
    _rate = _overruns = _silentFrames = _lastWriteMs = 0;
    _channel = -1;
    ++_generation;
    portEXIT_CRITICAL(&_mux);
}
bool PlaybackAudioTap::enabled() const { return _enabled.load(std::memory_order_relaxed); }
void PlaybackAudioTap::writeStereo(const int16_t* pcm, size_t frames, uint32_t sampleRate, uint32_t nowMs) {
    if (!enabled() || !pcm || !frames || sampleRate < 8000 || sampleRate > 192000) return;
    // Critical sections copy at most 128 frames. Choosing one stable channel
    // avoids cancellation of anti-phase stereo and switches off a silent side.
    for (size_t pos = 0; pos < frames; pos += 128) {
        size_t n = std::min<size_t>(128, frames - pos);
        uint32_t levels[2] = {};
        for (size_t i = 0; i < n; ++i) for (int c = 0; c < 2; ++c)
            levels[c] += (uint32_t)abs((int)pcm[2 * (pos + i) + c]);
        portENTER_CRITICAL(&_mux);
        if (!_enabled.load(std::memory_order_relaxed)) { portEXIT_CRITICAL(&_mux); return; }
        // A pause/reconnect must not splice separate waveform periods.
        if(nowMs && _lastWriteMs && nowMs-_lastWriteMs>250) {
            _count=0;_tail=_head;++_generation;
        }
        _lastWriteMs=nowMs;
        if (_rate != sampleRate) {
            _rate = sampleRate; _count = 0; _tail = _head;
            _channel = -1; _silentFrames = 0; ++_generation;
        }
        if (_channel < 0 && std::max(levels[0], levels[1]) > n * 8)
            _channel = levels[1] > levels[0] ? 1 : 0;
        int channel = _channel < 0 ? 0 : _channel;
        if (levels[channel] < n * 8 && levels[1 - channel] > n * 32) _silentFrames += n;
        else _silentFrames = 0;
        if (_silentFrames >= sampleRate / 4) {
            channel = _channel = 1 - channel; _silentFrames = 0;
            _count = 0; _tail = _head; ++_generation;
        }
        if (_count + n > CAPACITY) {
            size_t lost = _count + n - CAPACITY;
            _tail = (_tail + lost) % CAPACITY; _count -= lost;
            ++_overruns; ++_generation;
        }
        for (size_t i = 0; i < n; ++i) {
            _samples[_head] = pcm[2 * (pos + i) + channel];
            _head = (_head + 1) % CAPACITY;
        }
        _count += n;
        portEXIT_CRITICAL(&_mux);
    }
}
size_t PlaybackAudioTap::read(int16_t* mono, size_t frames, uint32_t& rate, uint32_t& generation) {
    if (!mono) return 0;
    portENTER_CRITICAL(&_mux);
    size_t n = std::min<size_t>(std::min<size_t>(frames, 512), _count);
    rate = _rate; generation = _generation;
    for (size_t i = 0; i < n; ++i) { mono[i] = _samples[_tail]; _tail = (_tail + 1) % CAPACITY; }
    _count -= n;
    portEXIT_CRITICAL(&_mux);
    return n;
}
uint32_t PlaybackAudioTap::overruns() {
    portENTER_CRITICAL(&_mux);
    uint32_t result = _overruns;
    portEXIT_CRITICAL(&_mux);
    return result;
}
