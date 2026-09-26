#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <driver/ledc.h>
#include <Arduino_GFX_Library.h>
#include "PokoPins.h"
#include "es8311.h"

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
    ledcWriteChannel(POKO_BL_PWM_CHANNEL, 200); // ~78% default
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
static es8311_handle_t _es8311Handle = nullptr;

inline bool initES8311(uint32_t sampleRate = 44100) {
    Wire.begin(POKO_PIN_I2C_SDA, POKO_PIN_I2C_SCL);

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
        return false;
    }
    es8311_sample_frequency_config(_es8311Handle, sampleRate * 256, sampleRate);
    es8311_voice_volume_set(_es8311Handle, 75, nullptr);
    es8311_microphone_config(_es8311Handle, false); // analog mic
    es8311_microphone_gain_set(_es8311Handle, ES8311_MIC_GAIN_30DB);

    // Power on speaker amplifier
    pinMode(POKO_PIN_PA_CTRL, OUTPUT);
    digitalWrite(POKO_PIN_PA_CTRL, HIGH);

    Serial.println("[ES8311] ok");
    return true;
}

inline void es8311SetVolume(int vol0to100) {
    if (_es8311Handle) {
        int v = constrain(vol0to100, 0, 100);
        es8311_voice_volume_set(_es8311Handle, v, nullptr);
    }
}

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

// ── I2S Driver ───────────────────────────────────────────────
//  Shared I2S port. Call initI2S() before using SnapPlayer /
//  TCPAudio / SyncedAVPlayer. One app owns I2S at a time.
static const i2s_port_t POKO_I2S_PORT = I2S_NUM_0;

inline esp_err_t initI2S(uint32_t sampleRate = 44100,
                          uint8_t  channels   = 2,
                          uint8_t  bitsPerSample = 16) {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = sampleRate,
        .bits_per_sample      = (i2s_bits_per_sample_t)bitsPerSample,
        .channel_format       = (channels == 1)
                                    ? I2S_CHANNEL_FMT_ALL_LEFT
                                    : I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = POKO_PIN_I2S_MCLK,
        .bck_io_num   = POKO_PIN_I2S_BCLK,
        .ws_io_num    = POKO_PIN_I2S_LRC,
        .data_out_num = POKO_PIN_I2S_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(POKO_I2S_PORT, &cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[I2S] driver install failed: %d\n", err);
        return err;
    }
    err = i2s_set_pin(POKO_I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[I2S] set_pin failed: %d\n", err);
    }
    return err;
}

inline void deinitI2S() {
    i2s_driver_uninstall(POKO_I2S_PORT);
}

// ── WS2812B LEDs ─────────────────────────────────────────────
//  Managed via FastLED. Call initLEDs() once, then use FastLED
//  API or the helpers below.
#include <FastLED.h>
static CRGB _leds[POKO_LED_COUNT];

inline void initLEDs() {
    FastLED.addLeds<WS2812B, POKO_PIN_LED_DATA, RGB>(_leds, POKO_LED_COUNT);
    FastLED.setBrightness(30);
    fill_solid(_leds, POKO_LED_COUNT, CRGB::Black);
    FastLED.show();
}

inline void setAllLEDs(CRGB color) {
    fill_solid(_leds, POKO_LED_COUNT, color);
    FastLED.show();
}

inline void setLEDBrightness(uint8_t b) {
    FastLED.setBrightness(b);
    FastLED.show();
}

inline void turnOffLEDs() {
    fill_solid(_leds, POKO_LED_COUNT, CRGB::Black);
    FastLED.show();
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
