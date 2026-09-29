#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>

#include "PokoPins.h"
#include "PokoAppState.h"
#include "ButtonInput.h"
#include "PokoDrivers.h"
#include "PokoUI.h"
#include "InfoApp.h"
#include "ClockApp.h"
#include "SSyncApp.h"
#include "MusicApp.h"
#include "VideoApp.h"
#include "GalleryApp.h"
#include "PixelApp.h"
#include "SettingsApp.h"
#include "PokoOTA.h"
#include "PokoAPI.h"
#include "AudioManager.h"
#include "BatteryManager.h"
#include "PowerManager.h"

// ─────────────────────────────────────────────────────────────
//  Poko Core Firmware — Complete 8-App Suite
//  Board: Waveshare ESP32-S3-LCD-0.85
//  Display: 128×128 GC9107 IPS
//  Codec: ES8311 + PA Amp
//  Controls: L = BOOT (GPIO 0), R = PLUS (GPIO 4), PWR = Dedicated Power (GPIO 5)
// ─────────────────────────────────────────────────────────────

WebServer   server(80);
Preferences prefs;
DNSServer   dnsServer;
ButtonInput btnInput;
BatteryManager batteryManager;
PowerManager*  powerManager      = nullptr;

// UI & Apps
PokoUI*      pokoUI              = nullptr;
InfoApp*     infoAppInstance     = nullptr;
ClockApp*    clockAppInstance    = nullptr;
SSyncApp*    ssyncAppInstance    = nullptr;
MusicApp*    musicAppInstance    = nullptr;
VideoApp*    videoAppInstance    = nullptr;
GalleryApp*  galleryAppInstance  = nullptr;
PixelApp*    pixelAppInstance    = nullptr;
SettingsApp* settingsAppInstance = nullptr;
SyncedAVPlayer* syncPlugin       = nullptr;
TCPAudio*       audioPlugin      = nullptr;
PokoAPI*     masterApi           = nullptr;
AudioManager* audioManager       = nullptr;
SnapPlayer*   snapService        = nullptr;

AppState activeApp = STATE_LAUNCHER;

// ── WiFi State Machine ────────────────────────────────────────
WifiModeState wifiState = STATE_WIFI_CONNECTING;
unsigned long wifiTimer = 0;
String savedSSID = "";
String savedPass = "";
String apPassword = "";
bool webServerStarted = false;
uint32_t staTimeoutMs = 15000;
uint32_t apTimeoutMs  = 120000;

bool startSetupAP() {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    WiFi.mode(WIFI_AP);
    apPassword = "";
    for (int i = 0; i < 8; ++i) {
        apPassword += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    if (!WiFi.softAP("POKO_SETUP", apPassword.c_str())) {
        apPassword = "";
        return false;
    }
    dnsServer.start(53, "*", WiFi.softAPIP());
    wifiState = STATE_WIFI_AP;
    wifiTimer = millis();
    return true;
}

void ensureWebServerStarted(const char* reason) {
    if (webServerStarted) return;
    server.begin(80);
    webServerStarted = true;
    Serial.printf("[web] server started (%s) at %s\n",
                  reason,
                  (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str());
}

String getNetworkStatusMsg() {
    if (wifiState == STATE_WIFI_CONNECTED) return "";
    if (wifiState == STATE_WIFI_AP) return "AP:POKO_SETUP";
    return "Connecting...";
}

const char* getAppName(AppState state) {
    switch (state) {
        case STATE_INFO:        return "Info";
        case STATE_CLOCK:       return "Clock";
        case STATE_SSYNC:       return "SSync";
        case STATE_MUSIC_UI:    return "Music";
        case STATE_VIDEO_UI:    return "Video";
        case STATE_GALLERY_UI:  return "Gallery";
        case STATE_PIXELS_UI:   return "Pixels";
        case STATE_SETTINGS_UI: return "Settings";
        case STATE_LAUNCHER:    return "Launcher";
        default:                return "App";
    }
}

// ── Themed App Loading Screen (Separate distinct designs for Dark & Light modes) ────
void showThemedLoadingScreen(AppState state) {
    if (!pokoGfx) return;
    const auto& theme = currentTheme();
    bool dark = isDarkTheme();

    int16_t x1, y1; uint16_t w, h;
    const char* appName = getAppName(state);

    if (dark) {
        // ── DARK THEME LOADING SCREEN ─────────────────────────────
        // Pure black background, subtle outline border, cyan accent pill,
        // white text rendered directly on black — no surface fill that could
        // appear "lighter/faded" against the surrounding black.
        pokoGfx->fillScreen(0x0000);

        // Outline-only rounded rect (no fill — avoids lighter card against black bg)
        pokoGfx->drawRoundRect(12, 32, 104, 64, 6, 0x18C3);  // dim cyan border

        // Glowing cyan accent bar
        pokoGfx->fillRoundRect(48, 40, 32, 3, 1, 0x07FF);

        char titleBuf[24];
        snprintf(titleBuf, sizeof(titleBuf), "Loading %s...", appName);
        pokoGfx->setFont(u8g2_font_helvB08_tf);
        pokoGfx->setTextColor(0xFFFF, 0x0000);  // white on pure black — no card bg
        pokoGfx->getTextBounds(titleBuf, 0, 0, &x1, &y1, &w, &h);
        if (w > 92) {
            const char* fallback = "Loading App...";
            pokoGfx->getTextBounds(fallback, 0, 0, &x1, &y1, &w, &h);
            pokoGfx->setCursor(64 - w / 2, 62);
            pokoGfx->print(fallback);
        } else {
            pokoGfx->setCursor(64 - w / 2, 62);
            pokoGfx->print(titleBuf);
        }

        pokoGfx->setFont(u8g2_font_5x7_tf);
        pokoGfx->setTextColor(0x4208, 0x0000);  // dim grey on black
        const char* sub = "PLEASE WAIT";
        pokoGfx->getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
        pokoGfx->setCursor(64 - w / 2, 79);
        pokoGfx->print(sub);

    } else {
        // ── LIGHT THEME LOADING SCREEN ────────────────────────────
        // Pure crisp white background, soft modern off-white card,
        // deep royal navy accent pill, jet black text, slate subtitle.
        pokoGfx->fillScreen(0xFFFF);

        pokoGfx->fillRoundRect(12, 32, 104, 64, 6, 0xF7BE); // theme.surface
        pokoGfx->drawRoundRect(12, 32, 104, 64, 6, 0xCE79); // theme.line border

        // Deep Royal Navy accent bar
        pokoGfx->fillRoundRect(48, 40, 32, 3, 1, 0x01F4);

        char titleBuf[24];
        snprintf(titleBuf, sizeof(titleBuf), "Loading %s...", appName);
        pokoGfx->setFont(u8g2_font_helvB08_tf);
        pokoGfx->setTextColor(0x0000, 0xF7BE);
        pokoGfx->getTextBounds(titleBuf, 0, 0, &x1, &y1, &w, &h);
        if (w > 92) {
            const char* fallback = "Loading App...";
            pokoGfx->getTextBounds(fallback, 0, 0, &x1, &y1, &w, &h);
            pokoGfx->setCursor(64 - w / 2, 62);
            pokoGfx->print(fallback);
        } else {
            pokoGfx->setCursor(64 - w / 2, 62);
            pokoGfx->print(titleBuf);
        }

        pokoGfx->setFont(u8g2_font_5x7_tf);
        pokoGfx->setTextColor(0x632C, 0xF7BE);
        const char* sub = "PLEASE WAIT";
        pokoGfx->getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
        pokoGfx->setCursor(64 - w / 2, 79);
        pokoGfx->print(sub);
    }
}

// ── Central App Switcher (Exclusive Resource Model) ───────────
void onAppChange(AppState newState) {
    if (newState == activeApp) return;

    Serial.printf("[app] switch %d -> %d\n", (int)activeApp, (int)newState);

    // Save last_app for safe apps across clean reboots
    if (newState == STATE_CLOCK ||
        newState == STATE_GALLERY_UI ||
        newState == STATE_SETTINGS_UI ||
        newState == STATE_INFO ||
        newState == STATE_PIXELS_UI ||
        newState == STATE_SSYNC ||
        newState == STATE_LAUNCHER) {
        prefs.putInt("last_app", (int)newState);
    }

    // Unload previous app resources
    if (activeApp == STATE_INFO && infoAppInstance)                  infoAppInstance->unload();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->unload();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->unload();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->unload();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->unload();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->unload();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->unload();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->unload();

    activeApp = newState;
    btnInput.setHoldRepeatEnabled(newState == STATE_PIXELS_UI || newState == STATE_SSYNC ||
                                  newState == STATE_MUSIC_UI || newState == STATE_VIDEO_UI ||
                                  newState == STATE_SETTINGS_UI);
    // Do not replay a held gesture into the newly opened application.
    btnInput.suppressUntilAllReleased();

    // Show themed loading screen between apps to eliminate black freeze flash
    if (newState == STATE_LAUNCHER) {
        pokoGfx->fillScreen(currentTheme().bg);
    } else {
        showThemedLoadingScreen(newState);
    }

    // Load newly active app
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->redraw();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->load();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->load();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->load();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->load();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->load();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->load();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->load();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->load();
}

void refreshActiveAppTheme() {
    if (activeApp == STATE_LAUNCHER && pokoUI)                      pokoUI->renderDirect();
    else if (activeApp == STATE_INFO && infoAppInstance)            infoAppInstance->refreshTheme();
    else if (activeApp == STATE_CLOCK && clockAppInstance)          clockAppInstance->refreshTheme();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)          ssyncAppInstance->refreshTheme();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)       musicAppInstance->refreshTheme();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)       videoAppInstance->refreshTheme();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)   galleryAppInstance->refreshTheme();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->refreshTheme();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance) settingsAppInstance->refreshTheme();
}

// ── Driver Reset Handler (Combo: Both held 5s) ────────────────
bool handleDriverReset() {
    Serial.println("[poko] performing driver reset");
    // Ensure all audio streaming tasks are safely stopped before resetting drivers
    if (audioManager) {
        audioManager->stopAll();
    }
    if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->unload();
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->unload();
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->unload();
    }
    if (audioPlugin) audioPlugin->unload();
    if (syncPlugin) syncPlugin->unload();
    vTaskDelay(pdMS_TO_TICKS(50));

    // Never reset shared I2S/display drivers while a worker still owns them.
    if ((audioPlugin && audioPlugin->isLoaded()) ||
        (syncPlugin && syncPlugin->isLoaded()) ||
        (audioManager && audioManager->hasActiveSession())) {
        Serial.println("[poko] driver reset aborted: media worker did not shut down safely");
        return false;
    }

    driverReset(pokoGfx);
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->redraw();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->load();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->load();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->load();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->load();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->load();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->load();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->load();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->load();
    return true;
}

// ── Power-aware Button Wake & Action Filter ──────────────────
bool handleButtonWakeCheck(bool isVolumeAction = false) {
    if (!powerManager) return true;
    if (powerManager->getDisplayState() == DISPLAY_POWER_OFF || powerManager->getDisplayState() == DISPLAY_POWER_SLEEP) {
        if (isVolumeAction && audioManager && audioManager->hasActiveSession()) {
            // Audio playing + screen off + volume action: allow action without waking display
            return true;
        }
        // Screen off and no audio (or non-volume action): wake display and consume event
        powerManager->wakeDisplay();
        return false;
    }
    // Screen is on (active or dimmed): notify activity and allow action
    powerManager->notifyUserActivity(ACTIVITY_BUTTON);
    return true;
}

// ── Button & Combo Callbacks ──────────────────────────────────
void onBtnLeft() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[action] Left (BOOT) Clicked");
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->navigateLeft();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->onLeft();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->onLeft();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onLeft();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->onLeft();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->onLeft();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->onLeft();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->onLeft();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->onLeft();
}

void onBtnRight() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[action] Right (KEY) Clicked");
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->navigateRight();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->onRight();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->onRight();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onRight();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->onRight();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->onRight();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->onRight();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->onRight();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->onRight();
}

int acceleratedVolumeStep(bool increasing) {
    static uint32_t holdStartMs = 0;
    static uint32_t lastHoldMs = 0;
    static AppState holdApp = STATE_LAUNCHER;
    static bool lastIncreasing = false;
    uint32_t now = millis();
    if (holdApp != activeApp || lastIncreasing != increasing || now - lastHoldMs > 350) holdStartMs = now;
    holdApp = activeApp;
    lastIncreasing = increasing;
    lastHoldMs = now;
    uint32_t held = now - holdStartMs;
    return held < 1000 ? 2 : held < 2500 ? 5 : 10;
}

void onBtnLeftHolding() {
    if (!handleButtonWakeCheck(true)) return;
    if (activeApp == STATE_PIXELS_UI && pixelAppInstance) {
        pixelAppInstance->onHoldingLeft();
    } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->volumeRampDown(acceleratedVolumeStep(false));
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->volumeRampDown(acceleratedVolumeStep(false));
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->volumeRampDown(acceleratedVolumeStep(false));
    } else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance) {
        settingsAppInstance->onHoldingLeft();
    }
}

void onBtnRightHolding() {
    if (!handleButtonWakeCheck(true)) return;
    if (activeApp == STATE_PIXELS_UI && pixelAppInstance) {
        pixelAppInstance->onHoldingRight();
    } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->volumeRampUp(acceleratedVolumeStep(true));
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->volumeRampUp(acceleratedVolumeStep(true));
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->volumeRampUp(acceleratedVolumeStep(true));
    } else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance) {
        settingsAppInstance->onHoldingRight();
    }
}

void onBtnLeftDouble() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[action] Left Double-Click -> Exit / Back");
    if (activeApp == STATE_LAUNCHER) {
        // Double-click Left on Home screen: Stop current active audio service and remove dot
        if (audioManager && audioManager->hasActiveSession()) {
            Serial.println("[action] Home Double-L -> Stop Active Audio Session");
            audioManager->stopActiveSession();
            if (pokoUI) pokoUI->updateStatusBar();
            return;
        }
    } else if (activeApp == STATE_INFO && infoAppInstance) {
        infoAppInstance->onBack();
    } else if (activeApp == STATE_GALLERY_UI && galleryAppInstance) {
        galleryAppInstance->onBack();
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->onBack();
    } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->onBack();
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->onBack();
    } else {
        onAppChange(STATE_LAUNCHER);
    }
}

void onBtnRightDouble() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[action] Right Double-Click -> Enter / Action");
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->enter();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->onBack();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->onEnter();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onEnter();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->onEnter();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->onEnter();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->onEnter();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->onEnter();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->onEnter();
}

void onBtnLongRight() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[action] Right Long-Press");
    if (activeApp == STATE_INFO && infoAppInstance)                  infoAppInstance->onLongRight();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onLongRight();
}

// ── Dual Button Combos ────────────────────────────────────────
void onComboBothClick() {
    if (!handleButtonWakeCheck(true)) return;
    Serial.println("[combo] both click -> Toggle Audio Play/Pause or LED");
    if (audioManager && audioManager->hasActiveSession()) {
        audioManager->togglePlayPause();
        if (pokoUI && activeApp == STATE_LAUNCHER) pokoUI->updateStatusBar();
        return;
    }
    static bool ledOn = false;
    ledOn = !ledOn;
    if (ledOn) {
        setAllLEDs(CRGB(0, 180, 255));
    } else {
        turnOffLEDs();
    }
}

void onComboBothDouble() {
    if (!handleButtonWakeCheck(false)) return;
    Serial.println("[combo] both double-click -> Jump to InfoApp");
    onAppChange(STATE_INFO);
}

void onComboBothLong() {
    Serial.println("[combo] Both L+R held 2.5s -> Clean Reboot");
    prefs.putBool("clean_shutdown", true);
    pokoGfx->fillScreen(POKO_CLR_ERR);
    pokoGfx->setFont(u8g2_font_helvB10_tf);
    pokoGfx->setTextColor(POKO_CLR_TEXT);
    int16_t x1, y1; uint16_t w, h;
    const char* msg = "REBOOTING...";
    pokoGfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    pokoGfx->setCursor(64 - w / 2, 68);
    pokoGfx->print(msg);
    pokoGfx->flush();
    delay(500);
    if (powerManager) powerManager->powerOff(true);
    else              ESP.restart();
}

void onComboBothVLong() {
    Serial.println("[combo] both held 5s -> Driver Reset");
    handleDriverReset();
}

void onComboBothUltra() {
    Serial.println("[combo] both held 10s -> Rebooting");
    prefs.putBool("clean_shutdown", true);
    pokoGfx->fillScreen(RGB565_RED);
    delay(500);
    ESP.restart();
}

// ── Arduino Setup ─────────────────────────────────────────────
void setup() {
    powerLatchOn();
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n========================================");
    Serial.println("           POKO CORE INIT               ");
    Serial.println("========================================");

    // Preferences & Settings
    prefs.begin("poko", false);
    batteryManager.begin();
    Wire.setTimeOut(50); // 50ms bus timeout prevents hardware lockups

    // Hardware reset reason & crash recovery check
    esp_reset_reason_t rstReason = esp_reset_reason();
    bool isCrashRecovery = (rstReason == ESP_RST_PANIC || 
                            rstReason == ESP_RST_INT_WDT || 
                            rstReason == ESP_RST_TASK_WDT || 
                            rstReason == ESP_RST_WDT ||
                            rstReason == ESP_RST_BROWNOUT);
    bool cleanShutdown = prefs.getBool("clean_shutdown", false);
    prefs.putBool("clean_shutdown", false);

    // Track consecutive crashes to prevent endless reboot loops
    uint32_t crashCount = prefs.getUInt("crash_count", 0);
    if (isCrashRecovery) {
        crashCount++;
        prefs.putUInt("crash_count", crashCount);
        Serial.printf("[poko] crash detected (consecutive=%u, reason=%d)\n", crashCount, (int)rstReason);
    }

    bool safeMode = (crashCount >= 3);
    if (safeMode) {
        Serial.println("[poko] *** SAFE MODE ACTIVATED: 3+ consecutive crashes ***");
        prefs.putBool("snap_auto", false);
        prefs.putInt("brightness", 40);
        prefs.putUInt("crash_count", 0);
    }
    Serial.printf("[poko] boot: reset_reason=%d, clean_shutdown=%s, safe_mode=%s\n",
                  (int)rstReason, cleanShutdown ? "true" : "false", safeMode ? "YES" : "no");

    // Task Watchdog Timer (TWDT) with 15s timeout to catch infinite loops or driver deadlocks
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&twdt_config);
    esp_task_wdt_add(NULL);

    AppState initialApp = STATE_LAUNCHER;
    int savedApp = prefs.getInt("last_app", (int)STATE_LAUNCHER);

    // Restore previous safe foreground app unless recovering from crash or in safe mode
    if (!isCrashRecovery && !safeMode) {
        if (savedApp >= 0 && savedApp < STATE_COUNT && 
            savedApp != STATE_VIDEO_UI && savedApp != STATE_MUSIC_UI) {
            initialApp = (AppState)savedApp;
            Serial.printf("[poko] restoring previous safe foreground app: %d\n", (int)initialApp);
        }
    } else {
        Serial.println("[poko] crash recovery / safe mode: booting safely to Launcher");
    }

    // LittleFS Storage for offline photos & assets
    if (!LittleFS.begin(false)) {
        Serial.println("[fs] LittleFS mount failed!");
    } else {
        Serial.printf("[fs] LittleFS ready (%u / %u bytes used)\n",
                      (unsigned int)LittleFS.usedBytes(),
                      (unsigned int)LittleFS.totalBytes());
        if (!LittleFS.exists("/photos")) {
            LittleFS.mkdir("/photos");
        }
    }

    // 0. Theme Init
    String savedTheme = prefs.getString("ui_theme", "dark");
    setPokoTheme(savedTheme != "light");

    // 1. Centralized Display Init
    createDisplay();
    if (!initDisplay(pokoGfx)) {
        Serial.println("[display] init failed!");
    } else {
        Serial.println("[display] 128x128 GC9107 ready");
    }

    // 2. Backlight (attached, kept OFF until UI is drawn)
    initBacklight();

    // 3. Audio Codec (ES8311) & I2S Master Clock
    ensureAudioOutput(44100);
    int savedMaster = prefs.getInt("master_vol", 100);
    setMasterVolumeLimit(savedMaster);
    int savedVol = prefs.getInt("volume", 75);
    setScaledVolume(savedVol);
    int savedBoost = prefs.getInt("amp_boost", 0);
    setAmpBoostDb(savedBoost);

    // 4. WS2812B LEDs
    initLEDs();
    pixelEngine.loadFromPreferences(prefs);

    // 5. Button Input Setup (3 physical buttons: DOWN=0, UP=4, PWR=5)
    btnInput.onPress([]() -> bool {
        if (!powerManager) return false;
        auto state = powerManager->getDisplayState();
        if (state == DISPLAY_POWER_OFF || state == DISPLAY_POWER_SLEEP) {
            powerManager->wakeDisplay();
            return true;
        }
        powerManager->notifyUserActivity(ACTIVITY_BUTTON);
        return false;
    });
    btnInput.onDown(onBtnLeft);
    btnInput.onUp(onBtnRight);
    btnInput.onDownDouble(onBtnLeftDouble);
    btnInput.onUpDouble(onBtnRightDouble);
    btnInput.onDownHolding(onBtnLeftHolding);
    btnInput.onUpHolding(onBtnRightHolding);
    btnInput.onLongUp(onBtnLongRight);
    btnInput.onPwrClick([]() {
        Serial.println("[btn] PWR Click -> Toggle Screen");
        if (powerManager) powerManager->toggleScreen();
    });
    btnInput.onPwrLong([]() {
        Serial.println("[btn] PWR Long -> Graceful Shutdown");
        if (powerManager) powerManager->powerOff();
    });
    btnInput.onBothClick(onComboBothClick);
    btnInput.onBothDouble(onComboBothDouble);
    btnInput.onBothLong(onComboBothLong);
    btnInput.onBothVLong(onComboBothVLong);
    btnInput.onBothUltra(onComboBothUltra);
    btnInput.begin();

    // 6. Instantiate Central AudioManager & Background SnapPlayer Service
    audioManager = new AudioManager();
    snapService = new SnapPlayer(nullptr, &prefs);
    snapService->begin();
    audioManager->setSnapPlayer(snapService);

    // 7. Instantiate Central PowerManager
    powerManager = new PowerManager(&batteryManager, audioManager, &prefs);
    powerManager->begin();

    pokoUI = new PokoUI(pokoGfx, onAppChange);
    pokoUI->begin();

    infoAppInstance = new InfoApp(pokoGfx, onAppChange);
    infoAppInstance->begin();

    clockAppInstance = new ClockApp(pokoGfx, onAppChange);
    clockAppInstance->begin();

    ssyncAppInstance = new SSyncApp(pokoGfx, onAppChange, snapService);
    ssyncAppInstance->begin();

    musicAppInstance = new MusicApp(pokoGfx, onAppChange);
    musicAppInstance->begin();
    if (audioManager) {
        audioManager->setMusicHandlers(
            []() { if (musicAppInstance) musicAppInstance->stopPlaybackInternal(); },
            []() { if (musicAppInstance) musicAppInstance->togglePlayPause(); },
            []() -> bool { return musicAppInstance ? musicAppInstance->isPlaying() : false; },
            []() -> const char* { return musicAppInstance ? musicAppInstance->getCurrentTitle() : "Music"; },
            []() -> bool { return musicAppInstance ? musicAppInstance->hasServerError() : false; }
        );
    }

    videoAppInstance = new VideoApp(pokoGfx, onAppChange);
    videoAppInstance->begin();

    galleryAppInstance = new GalleryApp(pokoGfx, onAppChange);
    galleryAppInstance->begin();

    pixelAppInstance = new PixelApp(pokoGfx, onAppChange);
    pixelAppInstance->begin();

    settingsAppInstance = new SettingsApp(pokoGfx, onAppChange);
    settingsAppInstance->begin();

    // Streaming Audio & Synced AV Players
    syncPlugin = new SyncedAVPlayer(pokoGfx, 1236);
    audioPlugin = new TCPAudio(1235);

    // Register display wake callback to immediately repaint the active app
    if (powerManager) {
        powerManager->setWakeCallback([]() {
            if (activeApp == STATE_LAUNCHER && pokoUI) {
                pokoUI->renderDirect();
            } else if (activeApp == STATE_INFO && infoAppInstance) {
                infoAppInstance->renderToCanvas();
            } else if (activeApp == STATE_CLOCK && clockAppInstance) {
                clockAppInstance->load();
            } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
                ssyncAppInstance->renderToCanvas();
            } else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance) {
                settingsAppInstance->renderToCanvas();
            } else if (activeApp == STATE_PIXELS_UI && pixelAppInstance) {
                pixelAppInstance->renderToCanvas();
            }
        });
    }

    // 7. WiFi & Network Services
    savedSSID = prefs.getString("wifi_ssid", "");
    savedPass = prefs.getString("wifi_pass", "");

    // Configure NTP time sync for GMT+5:30 (19800s offset)
    configTime(19800, 0, "pool.ntp.org", "time.google.com");

    if (savedSSID.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
        wifiTimer = millis();
        wifiState = STATE_WIFI_CONNECTING;
        Serial.printf("[wifi] connecting to %s...\n", savedSSID.c_str());
    } else {
        if (startSetupAP()) {
            ensureWebServerStarted("first-time AP");
            Serial.println("[wifi] setup AP started");
        } else {
            Serial.println("[wifi] setup AP failed to start");
        }
    }

    // 8. REST API & Web Dashboard
    masterApi = new PokoAPI(&server, &prefs);
    masterApi->begin();

    // Captive portal redirect in AP mode
    server.onNotFound([]() {
        if (WiFi.getMode() & WIFI_AP) {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    // 9. OTA Updates (Web & ArduinoOTA)
    PokoOTA::begin(&server, onAppChange, pokoGfx);

    // Initial state: Start on restored safe app or Launcher
    activeApp = initialApp;
    btnInput.setHoldRepeatEnabled(initialApp == STATE_PIXELS_UI || initialApp == STATE_SSYNC ||
                                  initialApp == STATE_MUSIC_UI || initialApp == STATE_VIDEO_UI ||
                                  initialApp == STATE_SETTINGS_UI);
    btnInput.suppressUntilAllReleased();
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->renderDirect();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->load();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->load();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->load();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->load();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->load();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->load();

    // Turn ON Backlight now that the initial UI is fully rendered on screen
    int savedBr = prefs.getInt("brightness", 80);
    setBacklightPercent(savedBr);

    Serial.println(">>> POKO 8-APP SUITE READY <<<");
}

// ── Arduino Main Loop ─────────────────────────────────────────
void loop() {
    esp_task_wdt_reset(); // Keep Task Watchdog alive

    // Clear consecutive crash counter after 30 seconds of stable runtime
    static bool crashCountCleared = false;
    if (!crashCountCleared && millis() > 30000) {
        crashCountCleared = true;
        prefs.putUInt("crash_count", 0);
    }

    // Process button input and combos
    btnInput.update();
    if (audioManager) audioManager->update();
    if (powerManager) powerManager->update();

    // WiFi STA/AP Non-blocking State Machine
    if (wifiState == STATE_WIFI_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiState = STATE_WIFI_CONNECTED;
            ensureWebServerStarted("sta connected");
            Serial.printf("[wifi] connected! IP: %s\n", WiFi.localIP().toString().c_str());

            // Initialize ArduinoOTA once connected
            static bool otaInit = false;
            if (!otaInit) {
                ArduinoOTA.setHostname("Poko");
                ArduinoOTA.onStart([]() {
                    if (powerManager) {
                        powerManager->wakeDisplay();
                        powerManager->acquireLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                    }
                    if (audioManager) audioManager->stopAll();
                    if (snapService && snapService->isLoaded()) {
                        snapService->unload();
                    }
                    onAppChange(STATE_LAUNCHER);
                });
                ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                    if (total > 0) {
                        float pct = ((float)progress / (float)total) * 100.0f;
                        pixelEngine.showOtaProgress(pct);
                    }
                });
                ArduinoOTA.onError([](ota_error_t error) {
                    if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                    pixelEngine.showOtaError();
                });
                ArduinoOTA.onEnd([]() {
                    pixelEngine.showOtaProgress(100.0f);
                    if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                    Preferences p;
                    p.begin("poko", false);
                    p.putBool("clean_shutdown", true);
                    p.end();
                });
                ArduinoOTA.begin();
                otaInit = true;
            }

            if (activeApp == STATE_LAUNCHER && pokoUI) pokoUI->updateStatusBar();

            bool shouldAutoStartSnap = prefs.getBool("snap_auto", true) || (activeApp == STATE_SSYNC);
            if (shouldAutoStartSnap) {
                if (snapService && !snapService->isLoaded()) {
                    Serial.println("[snap] starting SSync service on wifi connect");
                    if (audioManager && audioManager->activeSource() != AUDIO_NONE) {
                        Serial.println("[snap] other audio active on wifi connect, loading SSync as suspended");
                        snapService->load(true);
                        audioManager->setSuspendedSource(AUDIO_SSYNC);
                    } else if (audioManager) {
                        audioManager->request(AUDIO_SSYNC);
                    }
                }
            }
        } else if (millis() - wifiTimer > staTimeoutMs) {
            Serial.println("[wifi] connection timeout -> fallback to AP mode");
            WiFi.disconnect();
            if (startSetupAP()) {
                ensureWebServerStarted("AP fallback");
            } else {
                wifiTimer = millis();
                Serial.println("[wifi] AP fallback failed to start");
            }
            if (activeApp == STATE_LAUNCHER && pokoUI) pokoUI->updateStatusBar();
        }
    } else if (wifiState == STATE_WIFI_AP) {
        dnsServer.processNextRequest();
        if (WiFi.softAPgetStationNum() == 0 && (millis() - wifiTimer > apTimeoutMs) && savedSSID.length() > 0) {
            Serial.println("[wifi] AP timeout -> retrying STA mode");
            dnsServer.stop();
            WiFi.mode(WIFI_STA);
            WiFi.begin(savedSSID.c_str(), savedPass.c_str());
            wifiState = STATE_WIFI_CONNECTING;
            wifiTimer = millis();
            if (activeApp == STATE_LAUNCHER && pokoUI) pokoUI->updateStatusBar();
        } else if (WiFi.softAPgetStationNum() > 0) {
            wifiTimer = millis();
        }
    } else if (wifiState == STATE_WIFI_CONNECTED) {
        ArduinoOTA.handle();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] lost connection -> reconnecting");
            WiFi.disconnect();
            if (savedSSID.length() > 0) {
                WiFi.begin(savedSSID.c_str(), savedPass.c_str());
            } else {
                WiFi.reconnect();
            }
            wifiState = STATE_WIFI_CONNECTING;
            wifiTimer = millis();
            if (activeApp == STATE_LAUNCHER && pokoUI) pokoUI->updateStatusBar();
        }
    }

    // Web Server requests
    server.handleClient();

    // Active App execution
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->update();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->update();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->update();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->update();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->update();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->update();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->update();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->update();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->update();

    // Background music update (track position, auto-advance, neopixel progress) when not in foreground
    if (activeApp != STATE_MUSIC_UI && musicAppInstance && musicAppInstance->isPlaying()) {
        musicAppInstance->update();
    }

    // NeoPixel lighting engine update (with Music & SSync audio states)
    bool isMusicPlaying = (musicAppInstance && musicAppInstance->isPlaying());
    bool isSSyncPlaying = (snapService && snapService->isPlaying());
    pixelEngine.update(isMusicPlaying, isSSyncPlaying);
}

