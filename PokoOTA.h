#pragma once
#include <WebServer.h>
#include <Update.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include "PokoAppState.h"
#include "PixelEngine.h"
#include "SnapPlayer.h"
#include "AudioManager.h"

extern SnapPlayer* snapService;

// ─────────────────────────────────────────────────────────────
//  PokoOTA — Web-based OTA firmware updates and progress screen
// ─────────────────────────────────────────────────────────────

const char poko_ota_html[] PROGMEM = R"OTAHTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Poko OTA</title>
<style>
body{background:#0f1115;color:#edf0f3;font-family:system-ui,sans-serif;padding:24px;max-width:500px;margin:40px auto;text-align:center}
h2{margin-bottom:12px;color:#00c8ff}
p{color:#9da6b0;font-size:14px;margin-bottom:24px}
input[type=file]{background:#181b20;border:1px solid #30353d;padding:12px;border-radius:8px;width:100%;margin-bottom:16px;color:#edf0f3}
button{background:#00c8ff;color:#000;border:none;padding:12px 24px;border-radius:8px;font-weight:700;font-size:15px;cursor:pointer;width:100%}
.progress{background:#181b20;height:12px;border-radius:6px;overflow:hidden;margin-top:20px;display:none;border:1px solid #30353d}
.bar{height:100%;background:#00c8ff;width:0%}
</style>
</head>
<body>
<h2>Poko Firmware Update</h2>
<p>Upload compiled .bin to update device wirelessly.</p>
<form id="f" onsubmit="upload(event)">
  <input type="file" id="bin" accept=".bin" required>
  <button type="submit" id="btn">Flash Firmware</button>
  <div class="progress" id="prog"><div class="bar" id="bar"></div></div>
</form>
<script>
function upload(e){
  e.preventDefault();
  const file = document.getElementById('bin').files[0];
  if(!file) return;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/ota/upload', true);
  document.getElementById('prog').style.display='block';
  document.getElementById('btn').disabled=true;
  xhr.upload.onprogress = ev => {
    if(ev.lengthComputable){
      const p = Math.round((ev.loaded / ev.total)*100);
      document.getElementById('bar').style.width = p + '%';
      document.getElementById('btn').textContent = 'Flashing ' + p + '%';
    }
  };
  xhr.onload = () => {
    if(xhr.status === 200){
      alert('Update Successful! Rebooting...');
      setTimeout(()=>location.href='/', 3000);
    } else {
      alert('Update Failed: ' + xhr.responseText);
      document.getElementById('btn').disabled=false;
      document.getElementById('btn').textContent = 'Flash Firmware';
    }
  };
  const formData = new FormData();
  formData.append('update', file);
  xhr.send(formData);
}
</script>
</body>
</html>)OTAHTML";

class PokoOTA {
private:
    static inline size_t _otaTotal = 0;
    static inline size_t _otaWritten = 0;
    static inline int    _otaLastPct = -1;

    static void drawProgress(Arduino_GFX* gfx, int pct) {
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

public:
    static void begin(WebServer* server, AppSwitchFn switchCb, Arduino_GFX* gfx) {
        server->on("/ota", HTTP_GET, [server]() {
            server->sendHeader("Cache-Control", "no-store");
            server->send_P(200, "text/html; charset=utf-8", poko_ota_html);
        });

        server->on("/ota/upload", HTTP_POST, [server, gfx]() {
            server->sendHeader("Connection", "close");
            bool ok = !Update.hasError();
            if (!ok) {
                pixelEngine.showOtaError();
            } else {
                pixelEngine.showOtaProgress(100.0f);
            }
            server->send(200, "text/plain", ok ? "OK" : "FAIL");
            if (gfx) {
                gfx->fillScreen(ok ? 0x07E0 : 0xF800);
                gfx->setFont(u8g2_font_helvB10_tf);
                gfx->setTextColor(RGB565_BLACK);
                gfx->setCursor(20, 68);
                gfx->print(ok ? "SUCCESS!" : "FAILED!");
            }
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
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
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
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    drawProgress(gfx, 100);
                    pixelEngine.showOtaProgress(100.0f);
                } else {
                    Update.printError(Serial);
                    pixelEngine.showOtaError();
                }
            }
        });
    }
};
