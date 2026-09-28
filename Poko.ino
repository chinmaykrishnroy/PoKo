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

// ─────────────────────────────────────────────────────────────
//  Poko Core Firmware — Complete 8-App Suite
//  Board: Waveshare ESP32-S3-LCD-0.85
//  Display: 128×128 GC9107 IPS
//  Codec: ES8311 + PA Amp
//  Controls: BOOT (GPIO 0) = Left, KEY (GPIO 5 & 4) = Right
// ─────────────────────────────────────────────────────────────

WebServer   server(80);
Preferences prefs;
DNSServer   dnsServer;
ButtonInput btnInput;

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
bool webServerStarted = false;
uint32_t staTimeoutMs = 15000;
uint32_t apTimeoutMs  = 120000;

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

    // Blank screen cleanly between apps
    pokoGfx->fillScreen(RGB565_BLACK);

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

// ── Driver Reset Handler (Combo: Both held 5s) ────────────────
void handleDriverReset() {
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
    if (audioPlugin) audioPlugin->stopStream();
    if (syncPlugin) syncPlugin->unload();
    vTaskDelay(pdMS_TO_TICKS(50));

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
}

// ── Button & Combo Callbacks ──────────────────────────────────
void onBtnLeft() {
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

void onBtnLeftHolding() {
    if (activeApp == STATE_PIXELS_UI && pixelAppInstance) {
        pixelAppInstance->onHoldingLeft();
    } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->volumeRampDown();
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->volumeRampDown();
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->volumeRampDown();
    } else {
        int cur = getCurrentAppVolume();
        if (cur > 0) {
            setScaledVolume(max(0, cur - 2));
            prefs.putInt("volume", getCurrentAppVolume());
        }
    }
}

void onBtnRightHolding() {
    if (activeApp == STATE_PIXELS_UI && pixelAppInstance) {
        pixelAppInstance->onHoldingRight();
    } else if (activeApp == STATE_SSYNC && ssyncAppInstance) {
        ssyncAppInstance->volumeRampUp();
    } else if (activeApp == STATE_MUSIC_UI && musicAppInstance) {
        musicAppInstance->volumeRampUp();
    } else if (activeApp == STATE_VIDEO_UI && videoAppInstance) {
        videoAppInstance->volumeRampUp();
    } else {
        int cur = getCurrentAppVolume();
        if (cur < 100) {
            setScaledVolume(min(100, cur + 2));
            prefs.putInt("volume", getCurrentAppVolume());
        }
    }
}

void onBtnLeftDouble() {
    Serial.println("[action] Left Double-Click -> Exit / Back");
    if (activeApp == STATE_LAUNCHER) {
        // Double-click Left on Home screen: Stop current active audio service and remove dot
        if (audioManager && audioManager->hasActiveSession()) {
            Serial.println("[action] Home Double-L -> Stop Active Audio Session");
            audioManager->stopActiveSession();
            if (pokoUI) pokoUI->updateStatusBar();
            return;
        }
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
    Serial.println("[action] Right Double-Click -> Enter / Action");
    if (activeApp == STATE_LAUNCHER && pokoUI)                       pokoUI->enter();
    else if (activeApp == STATE_INFO && infoAppInstance)             infoAppInstance->onEnter();
    else if (activeApp == STATE_CLOCK && clockAppInstance)           clockAppInstance->onEnter();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onEnter();
    else if (activeApp == STATE_MUSIC_UI && musicAppInstance)        musicAppInstance->onEnter();
    else if (activeApp == STATE_VIDEO_UI && videoAppInstance)        videoAppInstance->onEnter();
    else if (activeApp == STATE_GALLERY_UI && galleryAppInstance)    galleryAppInstance->onEnter();
    else if (activeApp == STATE_PIXELS_UI && pixelAppInstance)      pixelAppInstance->onEnter();
    else if (activeApp == STATE_SETTINGS_UI && settingsAppInstance)  settingsAppInstance->onEnter();
}

void onBtnLongRight() {
    Serial.println("[action] Right Long-Press");
    if (activeApp == STATE_INFO && infoAppInstance)                  infoAppInstance->onLongRight();
    else if (activeApp == STATE_SSYNC && ssyncAppInstance)           ssyncAppInstance->onLongRight();
}

// ── Dual Button Combos ────────────────────────────────────────
void onComboBothClick() {
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
    Serial.println("[combo] both double-click -> Jump to InfoApp");
    onAppChange(STATE_INFO);
}

void onComboBothLong() {
    int cur = prefs.getInt("brightness", 80);
    int next = 80;
    if (cur <= 30)      next = 60;
    else if (cur <= 65) next = 100;
    else                next = 25;

    prefs.putInt("brightness", next);
    setBacklightPercent(next);
    Serial.printf("[combo] both held 2s -> brightness %d%%\n", next);
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
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n========================================");
    Serial.println("           POKO CORE INIT               ");
    Serial.println("========================================");

    // Preferences & Settings
    prefs.begin("poko", false);

    // Hardware reset reason & clean shutdown check
    esp_reset_reason_t rstReason = esp_reset_reason();
    bool isCrashRecovery = (rstReason == ESP_RST_PANIC || 
                            rstReason == ESP_RST_INT_WDT || 
                            rstReason == ESP_RST_TASK_WDT || 
                            rstReason == ESP_RST_WDT);
    bool cleanShutdown = prefs.getBool("clean_shutdown", false);
    prefs.putBool("clean_shutdown", false);
    Serial.printf("[poko] boot: reset_reason=%d, clean_shutdown=%s\n", (int)rstReason, cleanShutdown ? "true" : "false");

    AppState initialApp = STATE_LAUNCHER;
    int savedApp = prefs.getInt("last_app", (int)STATE_LAUNCHER);

    // Independent foreground app restoration: Restore previous safe foreground app unless recovering from crash
    if (!isCrashRecovery) {
        if (savedApp >= 0 && savedApp < STATE_COUNT && 
            savedApp != STATE_VIDEO_UI && savedApp != STATE_MUSIC_UI) {
            initialApp = (AppState)savedApp;
            Serial.printf("[poko] restoring previous safe foreground app: %d\n", (int)initialApp);
        }
    } else {
        Serial.println("[poko] crash recovery: booting safely to Launcher");
    }

    // LittleFS Storage for offline photos & assets
    if (!LittleFS.begin(true)) {
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

    // 5. Button Input Setup
    btnInput.begin();
    btnInput.onLeft(onBtnLeft);
    btnInput.onRight(onBtnRight);
    btnInput.onLeftDouble(onBtnLeftDouble);
    btnInput.onRightDouble(onBtnRightDouble);
    btnInput.onLeftHolding(onBtnLeftHolding);
    btnInput.onRightHolding(onBtnRightHolding);
    btnInput.onLongRight(onBtnLongRight);
    btnInput.onBothClick(onComboBothClick);
    btnInput.onBothDouble(onComboBothDouble);
    btnInput.onBothLong(onComboBothLong);
    btnInput.onBothVLong(onComboBothVLong);
    btnInput.onBothUltra(onComboBothUltra);

    // 6. Instantiate Central AudioManager & Background SnapPlayer Service
    audioManager = new AudioManager();
    snapService = new SnapPlayer(nullptr, &prefs);
    snapService->begin();
    audioManager->setSnapPlayer(snapService);

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
            []() { if (musicAppInstance) musicAppInstance->stopPlayback(); },
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
        // No saved WiFi -> start setup AP with password 12345678
        WiFi.mode(WIFI_AP);
        WiFi.softAP("POKO_SETUP", "12345678");
        dnsServer.start(53, "*", WiFi.softAPIP());
        wifiState = STATE_WIFI_AP;
        wifiTimer = millis();
        ensureWebServerStarted("first-time AP");
        Serial.println("[wifi] no credentials, started POKO_SETUP AP (password: 12345678)");
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

    Serial.println(">>> POKO 7-APP SUITE READY <<<");
}

// ── Arduino Main Loop ─────────────────────────────────────────
void loop() {
    // Process button input and combos
    btnInput.update();

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
                    pixelEngine.showOtaError();
                });
                ArduinoOTA.onEnd([]() {
                    pixelEngine.showOtaProgress(100.0f);
                    Preferences p;
                    p.begin("poko", false);
                    p.putBool("clean_shutdown", true);
                    p.end();
                });
                ArduinoOTA.begin();
                otaInit = true;
            }

            if (activeApp == STATE_LAUNCHER && pokoUI) pokoUI->updateStatusBar();

            if (prefs.getBool("snap_auto", true)) {
                if (snapService && !snapService->isLoaded()) {
                    Serial.println("[snap] auto-starting background SSync service");
                    snapService->load();
                    if (audioManager) audioManager->request(AUDIO_SSYNC);
                }
            }
        } else if (millis() - wifiTimer > staTimeoutMs) {
            Serial.println("[wifi] connection timeout -> fallback to AP mode");
            WiFi.disconnect();
            WiFi.mode(WIFI_AP);
            WiFi.softAP("POKO_SETUP", "12345678");
            dnsServer.start(53, "*", WiFi.softAPIP());
            wifiState = STATE_WIFI_AP;
            wifiTimer = millis();
            ensureWebServerStarted("AP fallback (password: 12345678)");
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
