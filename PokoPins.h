#pragma once

// ─────────────────────────────────────────────────────────────
//  Poko — Pin Definitions
//  Board: Waveshare ESP32-S3-LCD-0.85
//  MCU:   ESP32-S3, 8 MB Flash, 8 MB OPI PSRAM
// ─────────────────────────────────────────────────────────────

// ── Display (GC9107 IPS, 128×128, SPI) ───────────────────────
#define POKO_PIN_LCD_DC     45
#define POKO_PIN_LCD_CS     21
#define POKO_PIN_LCD_SCK    38
#define POKO_PIN_LCD_MOSI   39
#define POKO_PIN_LCD_RST    40
#define POKO_PIN_LCD_BL     46   // Backlight (PWM via ledc)

// ── Audio Codec (ES8311) ─────────────────────────────────────
#define POKO_PIN_I2C_SDA    42
#define POKO_PIN_I2C_SCL    41
#define POKO_PIN_PA_CTRL    7    // HIGH = speaker amplifier on

// ── I2S Bus (shared between ES8311 output and mic input) ─────
#define POKO_PIN_I2S_MCLK   8
#define POKO_PIN_I2S_BCLK   9
#define POKO_PIN_I2S_DOUT   12   // DAC out to ES8311
#define POKO_PIN_I2S_LRC    10   // LR clock

// ── WS2812B RGB LEDs (8 LEDs) ────────────────────────────────
#define POKO_PIN_LED_DATA   48
#define POKO_LED_COUNT      8

// ── Buttons ──────────────────────────────────────────────────
//   BOOT button (GPIO 0) = Left navigation
//   KEY  button (GPIO 5 & GPIO 4) = Right navigation (support both)
#define POKO_PIN_BTN_LEFT    0    // BOOT button
#define POKO_PIN_BTN_RIGHT1  5    // KEY button 1
#define POKO_PIN_BTN_RIGHT2  4    // KEY button 2
#define POKO_PIN_BTN_RIGHT   5

// ── Backlight PWM ────────────────────────────────────────────
#define POKO_BL_PWM_CHANNEL 0
#define POKO_BL_PWM_FREQ    5000
#define POKO_BL_PWM_RES     8    // 8-bit duty (0–255)
