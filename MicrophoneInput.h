#pragma once
#include <stddef.h>
#include <stdint.h>
#include <driver/i2s_std.h>

// Owned by AudioManager; start/stop/read are serialized on the main loop.
// Uses the board's ES7210 (0x40), MIC1/2 I2S output on GPIO11.
class MicrophoneInput {
public:
    MicrophoneInput();
    bool start();
    void stop();
    bool running() const;
    bool setGain(int db);
    int gain() const;
    size_t read(int16_t* mono, size_t frames);
    static constexpr uint32_t SAMPLE_RATE = 48000;
private:
    i2s_chan_handle_t _rx;
    int _gain;
    bool writeReg(uint8_t reg, uint8_t value);
    bool configure();
};
extern MicrophoneInput microphoneInput;
