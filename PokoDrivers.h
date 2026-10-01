#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <driver/i2s_std.h>
#include "PixelEngine.h"

// Central hardware driver interface. Mutable driver state is owned exclusively
// by PokoDrivers.cpp so every firmware translation unit sees the same devices.
extern Arduino_DataBus* pokoBus;
extern Arduino_GFX* pokoGfx;

extern i2s_chan_handle_t poko_tx_handle;
extern uint32_t poko_i2s_rate;
extern uint8_t poko_i2s_channels;
extern uint8_t poko_i2s_bits;

// Shared I2S buffering contract used by both the driver and SnapPlayer.
#define POKO_I2S_DMA_DESC_NUM      6
#define POKO_I2S_DMA_FRAME_NUM     240
#define POKO_I2S_DMA_BUFFER_FRAMES (POKO_I2S_DMA_DESC_NUM * POKO_I2S_DMA_FRAME_NUM)

Arduino_GFX* createDisplay();
bool initDisplay(Arduino_GFX* gfx);

void initBacklight();
void setBacklight(uint8_t duty);
void setBacklightPercent(int pct);

void powerLatchOn();
void powerLatchOff();

void displaySleep();
void displayWake();
bool isDisplayAsleep();

void setSpeakerAmp(bool enabled);
bool isSpeakerAmpEnabled();
void standbyAudioOutputHardware();
void restoreAudioOutputHardware();

esp_err_t initI2S(uint32_t sampleRate = 44100,
                  uint8_t channels = 2,
                  uint8_t bitsPerSample = 16);
void deinitI2S();

bool initES8311(uint32_t sampleRate = 44100);
bool reinitES8311(uint32_t sampleRate = 44100);
void es8311SetVolume(int vol0to100);
void es8311Mute(bool mute);

void setMasterVolumeLimit(int limit);
int getMasterVolumeLimit();
void setScaledVolume(int appVol0to100);
int getCurrentAppVolume();
void setAmpBoostDb(int db);
int getAmpBoostDb();

bool ensureAudioOutput(uint32_t sampleRate = 44100);
bool prepareAudioOutput(uint32_t sampleRate = 44100);

void initLEDs();
void setAllLEDs(CRGB color);
void setLEDBrightness(uint8_t brightness);
void turnOffLEDs();

void driverReset(Arduino_GFX* gfx);
