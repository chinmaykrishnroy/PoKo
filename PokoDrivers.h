#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/ledc.h>
#include <Arduino_GFX_Library.h>
#include "PokoPins.h"
#include "es8311.h"
#include "PixelEngine.h"

// ─────────────────────────────────────────────────────────────
//  PokoDrivers — Centralised hardware initialisation helpers
//  All hardware is init once at boot by Poko.ino.
//  Apps call the appropriate enable/disable helpers.
// ─────────────────────────────────────────────────────────────

// ── Display ──────────────────────────────────────────────────
//  Uses Arduino_GFX + GC9107 driver for the 128×128 IPS panel.
inline Arduino_DataBus* pokoBus = nullptr;
inline Arduino_GFX*     pokoGfx = nullptr;

inline Arduino_GFX* createDisplay() {
    pokoBus = new Arduino_ESP32SPI(
        POKO_PIN_LCD_DC,
        POKO_PIN_LCD_CS,
        POKO_PIN_LCD_SCK,
        POKO_PIN_LCD_MOSI,
        GFX_NOT_DEFINED   // no MISO
    );
    pokoGfx = new Arduino_GC9107(
        pokoBus,
        POKO_PIN_LCD_RST,
        0,    // rotation 0
        true  // IPS panel
    );
    return pokoGfx;
}

inline bool initDisplay(Arduino_GFX* gfx) {
    if (!gfx->begin()) return false;
    gfx->fillScreen(RGB565_BLACK);
    return true;
}

// ── Backlight (PWM) ──────────────────────────────────────────
inline void initBacklight() {
    ledcAttachChannel(POKO_PIN_LCD_BL, POKO_BL_PWM_FREQ, POKO_BL_PWM_RES, POKO_BL_PWM_CHANNEL);
    ledcWriteChannel(POKO_BL_PWM_CHANNEL, 0); // Keep OFF initially until UI is rendered
}

inline void setBacklight(uint8_t duty) {  // 0–255
    ledcWriteChannel(POKO_BL_PWM_CHANNEL, duty);
}

// Convert 0–100 percent to 0–255 duty
inline void setBacklightPercent(int pct) {
    pct = constrain(pct, 0, 100);
    setBacklight((pct * 255) / 100);
}

// ── ES8311 Audio Codec ────────────────────────────────────────
//  I2C: SDA=42, SCL=41
//  MCLK comes from I2S MCLK pin (pin 8).
inline es8311_handle_t _es8311Handle = nullptr;

inline void deinitI2S();

inline esp_err_t initI2S(uint32_t sampleRate = 44100,
                          uint8_t  channels   = 2,
                          uint8_t  bitsPerSample = 16);

inline bool initES8311(uint32_t sampleRate = 44100) {
    // ES8311 internal PLL requires MCLK actively driven on pin 8.
    // Ensure I2S is initialized and running before codec configuration.
    initI2S(sampleRate, 2, 16);

    Wire.begin(POKO_PIN_I2C_SDA, POKO_PIN_I2C_SCL);

    if (_es8311Handle) {
        es8311_delete(_es8311Handle);
        _es8311Handle = nullptr;
    }
    _es8311Handle = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
    if (!_es8311Handle) {
        Serial.println("[ES8311] create failed");
        return false;
    }

    const es8311_clock_config_t clk = {
        .mclk_inverted         = false,
        .sclk_inverted         = false,
        .mclk_from_mclk_pin    = true,
        .mclk_frequency        = (int)(sampleRate * 256),
        .sample_frequency      = (int)sampleRate
    };

    if (es8311_init(_es8311Handle, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("[ES8311] init failed");
        es8311_delete(_es8311Handle);
        _es8311Handle = nullptr;
        return false;
    }
    if (es8311_sample_frequency_config(_es8311Handle, sampleRate * 256, sampleRate) != ESP_OK) {
        Serial.printf("[ES8311] sample-rate setup failed (%lu Hz)\n", (unsigned long)sampleRate);
        es8311_delete(_es8311Handle);
        _es8311Handle = nullptr;
        return false;
    }
    es8311_voice_volume_set(_es8311Handle, 85, nullptr);
    es8311_voice_mute(_es8311Handle, false);
    es8311_microphone_config(_es8311Handle, false); // analog mic
    es8311_microphone_gain_set(_es8311Handle, ES8311_MIC_GAIN_30DB);

    // Power on speaker amplifier
    pinMode(POKO_PIN_PA_CTRL, OUTPUT);
    digitalWrite(POKO_PIN_PA_CTRL, HIGH);

    Serial.printf("[ES8311] DAC ready at %lu Hz; speaker amp enabled\n", (unsigned long)sampleRate);
    return true;
}

inline void es8311SetVolume(int vol0to100) {
    if (_es8311Handle) {
        int v = constrain(vol0to100, 0, 100);
        es8311_voice_volume_set(_es8311Handle, v, nullptr);
    }
}

static int _masterVolumeLimit = 100;
static int _currentAppVolume  = 75;

inline void setMasterVolumeLimit(int limit) {
    _masterVolumeLimit = constrain(limit, 0, 100);
    int effectiveVol = (_currentAppVolume * _masterVolumeLimit) / 100;
    pixelEngine.setEffectiveVolume(effectiveVol);
    if (_es8311Handle) es8311SetVolume(effectiveVol);
}

inline int getMasterVolumeLimit() {
    return _masterVolumeLimit;
}

inline void setScaledVolume(int appVol0to100) {
    _currentAppVolume = constrain(appVol0to100, 0, 100);
    int effectiveVol = (_currentAppVolume * _masterVolumeLimit) / 100;
    pixelEngine.setEffectiveVolume(effectiveVol);
    if (_es8311Handle) es8311SetVolume(effectiveVol);
}

inline int getCurrentAppVolume() {
    return _currentAppVolume;
}

static int _ampBoostDb = 0; // 0–5 dB extra gain above unity

inline void setAmpBoostDb(int db) {
    _ampBoostDb = constrain(db, 0, 5);
    if (_es8311Handle) {
        es8311_set_amp_boost_db(_es8311Handle, _ampBoostDb);
        // Re-apply current volume so the new cap takes effect immediately
        int effectiveVol = (_currentAppVolume * _masterVolumeLimit) / 100;
        es8311SetVolume(effectiveVol);
    }
}

inline int getAmpBoostDb() { return _ampBoostDb; }

inline void es8311Mute(bool mute) {
    if (_es8311Handle) es8311_voice_mute(_es8311Handle, mute);
}

inline bool reinitES8311(uint32_t sampleRate = 44100) {
    if (_es8311Handle) {
        es8311_delete(_es8311Handle);
        _es8311Handle = nullptr;
    }
    return initES8311(sampleRate);
}

// ── I2S Driver (Modern ESP-IDF 5.x / Arduino 3.x) ────────────
inline i2s_chan_handle_t poko_tx_handle = nullptr;
inline uint32_t          poko_i2s_rate = 0;
inline uint8_t           poko_i2s_channels = 0;
inline uint8_t           poko_i2s_bits = 0;

inline esp_err_t initI2S(uint32_t sampleRate,
                          uint8_t  channels,
                          uint8_t  bitsPerSample) {
    // Existing driver is usable ONLY when its format matches exactly
    if (poko_tx_handle) {
        if (poko_i2s_rate == sampleRate && poko_i2s_channels == channels && poko_i2s_bits == bitsPerSample) {
            return ESP_OK;
        }
        deinitI2S();
    }

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };

    esp_err_t err = i2s_new_channel(&chan_cfg, &poko_tx_handle, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[I2S] new_channel failed: %d\n", err);
        poko_tx_handle = nullptr;
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = sampleRate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = (i2s_data_bit_width_t)bitsPerSample,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = (uint32_t)bitsPerSample,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = (gpio_num_t)POKO_PIN_I2S_MCLK,
            .bclk = (gpio_num_t)POKO_PIN_I2S_BCLK,
            .ws   = (gpio_num_t)POKO_PIN_I2S_LRC,
            .dout = (gpio_num_t)POKO_PIN_I2S_DOUT,
            .din  = (gpio_num_t)I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };

    err = i2s_channel_init_std_mode(poko_tx_handle, &std_cfg);
    if (err != ESP_OK) {
        Serial.printf("[I2S] channel_init_std_mode failed: %d\n", err);
        i2s_del_channel(poko_tx_handle);
        poko_tx_handle = nullptr;
        return err;
    }

    err = i2s_channel_enable(poko_tx_handle);
    if (err != ESP_OK) {
        Serial.printf("[I2S] channel_enable failed: %d\n", err);
        i2s_del_channel(poko_tx_handle);
        poko_tx_handle = nullptr;
        return err;
    }

    poko_i2s_rate = sampleRate;
    poko_i2s_channels = channels;
    poko_i2s_bits = bitsPerSample;
    Serial.printf("[I2S] ready %lu Hz / %u ch / %u-bit\n", (unsigned long)sampleRate, channels, bitsPerSample);
    return ESP_OK;
}

inline void deinitI2S() {
    if (poko_tx_handle) {
        i2s_channel_disable(poko_tx_handle);
        i2s_del_channel(poko_tx_handle);
        poko_tx_handle = nullptr;
    }
    poko_i2s_rate = 0;
    poko_i2s_channels = 0;
    poko_i2s_bits = 0;
}

// ── Central Audio Output Enabler ──────────────────────────────
//  Guarantees I2S clock, ES8311 codec, and PA amplifier are
//  fully configured and unmuted for the requested sample rate.
inline bool ensureAudioOutput(uint32_t sampleRate = 44100) {
    if (!poko_tx_handle || poko_i2s_rate != sampleRate) {
        esp_err_t err = initI2S(sampleRate, 2, 16);
        if (err != ESP_OK) {
            Serial.printf("[audio] initI2S failed for %lu Hz: %d\n", (unsigned long)sampleRate, (int)err);
            return false;
        }
    }

    if (!_es8311Handle) {
        if (!initES8311(sampleRate)) {
            Serial.println("[audio] initES8311 failed");
            return false;
        }
    } else {
        es8311_sample_frequency_config(_es8311Handle, sampleRate * 256, sampleRate);
    }

    // Power on speaker amplifier
    pinMode(POKO_PIN_PA_CTRL, OUTPUT);
    digitalWrite(POKO_PIN_PA_CTRL, HIGH);

    // Ensure codec is active and volume is applied
    es8311Mute(false);
    setScaledVolume(getCurrentAppVolume());
    Serial.printf("[audio] output ready @ %lu Hz\n", (unsigned long)sampleRate);
    return true;
}

inline bool prepareAudioOutput(uint32_t sampleRate = 44100) {
    return ensureAudioOutput(sampleRate);
}

// ── WS2812B LEDs ─────────────────────────────────────────────
//  Managed via PixelEngine and FastLED. Call initLEDs() once.

inline void initLEDs() {
    pixelEngine.begin();
}

inline void setAllLEDs(CRGB color) {
    pixelEngine.setColor(color.r, color.g, color.b);
    pixelEngine.setTargetPixel(8);
    pixelEngine.setMode(PIXEL_MODE_SOLID);
}

inline void setLEDBrightness(uint8_t b) {
    pixelEngine.setBrightness(b);
}

inline void turnOffLEDs() {
    pixelEngine.setMode(PIXEL_MODE_OFF);
}

// ── Driver reset (called on 5-second both-hold combo) ────────
//  Re-initialises display and ES8311 without reboot.
inline void driverReset(Arduino_GFX* gfx) {
    Serial.println("[poko] driver reset");
    deinitI2S();
    delay(100);
    if (_es8311Handle) {
        es8311_delete(_es8311Handle);
        _es8311Handle = nullptr;
    }
    initES8311(44100);
    initI2S(44100);
    gfx->begin();
    gfx->fillScreen(RGB565_BLACK);
    initBacklight();
}
