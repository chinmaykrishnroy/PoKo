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

// ── Battery & Power Circuit ──────────────────────────────────
#define POKO_PIN_BAT_ADC     1    // Battery voltage divider (ADC1_CH0, 1:2 divider, multiplier = 3.0)
#define POKO_PIN_BAT_EN      2    // Power hold latch: HIGH = keep battery power ON, LOW = release / power off
#define POKO_PIN_CHARGING    3    // Charging status input: LOW = charging, HIGH = not charging / full (pullup enabled)

// ── Physical Buttons ─────────────────────────────────────────
//   BOOT / DOWN button (GPIO 0)
//   PLUS / UP   button (GPIO 4)
//   PWR         button (GPIO 5, dedicated system button)
#define POKO_PIN_BTN_DOWN    0    // BOOT / DOWN button
#define POKO_PIN_BTN_UP      4    // PLUS / UP button
#define POKO_PIN_BTN_PWR     5    // PWR button (power / sleep / wake)

// Legacy aliases for backward compatibility:
#define POKO_PIN_BTN_LEFT    POKO_PIN_BTN_DOWN
#define POKO_PIN_BTN_RIGHT   POKO_PIN_BTN_UP
#define POKO_PIN_BTN_RIGHT1  POKO_PIN_BTN_PWR
#define POKO_PIN_BTN_RIGHT2  POKO_PIN_BTN_UP

// ── Backlight PWM ────────────────────────────────────────────
#define POKO_BL_PWM_CHANNEL 0
#define POKO_BL_PWM_FREQ    5000
#define POKO_BL_PWM_RES     8    // 8-bit duty (0–255)

// ── Display Colors (RGB565) ──────────────────────────────────
#ifndef RGB565_BLACK
#define RGB565_BLACK    0x0000
#define RGB565_WHITE    0xFFFF
#define RGB565_RED      0xF800
#define RGB565_GREEN    0x07E0
#define RGB565_BLUE     0x001F
#define RGB565_CYAN     0x07FF
#define RGB565_MAGENTA  0xF81F
#define RGB565_YELLOW   0xFFE0
#define RGB565_GRAY     0x8410
#define RGB565_DARKGRAY 0x3186
#endif


