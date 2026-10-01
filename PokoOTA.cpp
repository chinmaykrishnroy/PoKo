#include "PokoOTA.h"

size_t PokoOTA::_otaTotal = 0;
size_t PokoOTA::_otaWritten = 0;
int PokoOTA::_otaLastPct = -1;

void PokoOTA::drawProgress(Arduino_GFX* gfx, int pct) {
        if (!gfx) return;
        if (_otaLastPct < 0) {
            gfx->fillScreen(RGB565_BLACK);
            gfx->setFont(u8g2_font_helvB10_tf);
            gfx->setTextColor(0x07FF, RGB565_BLACK);
            gfx->setCursor(22, 45);
            gfx->print("OTA UPDATE");

            gfx->drawRect(14, 65, 100, 16, RGB565_WHITE);
            _otaLastPct = 0;
        }

        if (pct != _otaLastPct) {
            _otaLastPct = pct;
            int filled = (96 * pct) / 100;
            gfx->fillRect(16, 67, filled, 12, 0x07FF);

            gfx->setFont(u8g2_font_profont10_mf);
            gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
            char buf[12];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            gfx->fillRect(45, 95, 40, 14, RGB565_BLACK);
            gfx->setCursor(55, 105);
            gfx->print(buf);
        }
    }

void PokoOTA::begin(WebServer* server, AppSwitchFn switchCb, Arduino_GFX* gfx) {
        server->on("/ota", HTTP_GET, [server]() {
            server->sendHeader("Cache-Control", "no-store");
            server->send_P(200, "text/html; charset=utf-8", poko_ota_html);
        });

        server->on("/ota/upload", HTTP_POST, [server, gfx]() {
            server->sendHeader("Connection", "close");
            bool ok = (!Update.hasError() && Update.isFinished());
            if (!ok) {
                if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                pixelEngine.showOtaError();
                server->send(500, "text/plain", "FAIL: firmware update failed");
                if (gfx) {
                    gfx->fillScreen(0xF800);
                    gfx->setFont(u8g2_font_helvB10_tf);
                    gfx->setTextColor(RGB565_BLACK);
                    gfx->setCursor(20, 68);
                    gfx->print("FAILED!");
                }
                return;
            }

            pixelEngine.showOtaProgress(100.0f);
            server->send(200, "text/plain", "OK");
            if (gfx) {
                gfx->fillScreen(0x07E0);
                gfx->setFont(u8g2_font_helvB10_tf);
                gfx->setTextColor(RGB565_BLACK);
                gfx->setCursor(20, 68);
                gfx->print("SUCCESS!");
            }
            if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
            {
                Preferences p;
                p.begin("poko", false);
                p.putBool("clean_shutdown", true);
                p.end();
            }
            delay(1000);
            ESP.restart();
        }, [server, switchCb, gfx]() {
            HTTPUpload& upload = server->upload();
            if (upload.status == UPLOAD_FILE_START) {
                // Wake display to full brightness and hold power lock during OTA
                if (powerManager) {
                    powerManager->wakeDisplay();
                    powerManager->acquireLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                }
                if (audioManager) audioManager->stopAll();
                if (snapService && snapService->isLoaded()) {
                    snapService->unload();
                }
                delay(50);
                if (switchCb) switchCb(STATE_LAUNCHER);
                if (Update.isRunning()) {
                    Update.abort();
                }
                _otaTotal = server->clientContentLength();
                _otaWritten = 0;
                _otaLastPct = -1;
                drawProgress(gfx, 0);
                pixelEngine.showOtaProgress(0.0f);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                    pixelEngine.showOtaError();
                    if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                esp_task_wdt_reset(); // Keep watchdog alive during large file writes
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                    pixelEngine.showOtaError();
                }
                _otaWritten += upload.currentSize;
                if (_otaTotal > 0) {
                    int pct = (_otaWritten * 100) / _otaTotal;
                    if (pct != _otaLastPct) {
                        drawProgress(gfx, pct);
                        pixelEngine.showOtaProgress((float)pct);
                    }
                }
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                pixelEngine.showOtaError();
                if (gfx) {
                    gfx->fillScreen(0xF800);
                    gfx->setFont(u8g2_font_helvB10_tf);
                    gfx->setTextColor(RGB565_BLACK);
                    gfx->setCursor(20, 68);
                    gfx->print("ABORTED!");
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    drawProgress(gfx, 100);
                    pixelEngine.showOtaProgress(100.0f);
                } else {
                    Update.printError(Serial);
                    pixelEngine.showOtaError();
                    if (powerManager) powerManager->releaseLock(POWER_LOCK_OTA | POWER_LOCK_DISPLAY, LOCK_OWNER_OTA);
                }
            }
        });
    }
