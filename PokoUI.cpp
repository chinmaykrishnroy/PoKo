#include "PokoUI.h"
#include <new>

void PokoUI::drawStatusBar() {
        if (!_statusCanvas) return;
        const auto& theme = currentTheme();
        _statusCanvas->fillScreen(theme.headerBg);

        // Left: WiFi status dot
        // Connecting -> Yellow blinking
        // Connected  -> Solid green
        // AP waiting -> Red blinking
        // AP client connected -> Solid red
        bool blinkPhase = ((millis() / 350) % 2 == 0);
        uint16_t dotColor = 0;
        bool showDot = true;

        if (wifiState == STATE_WIFI_CONNECTED) {
            dotColor = pokoClrGreen();
            showDot = true;
        } else if (wifiState == STATE_WIFI_CONNECTING) {
            dotColor = pokoClrWarn();
            showDot = blinkPhase;
        } else if (wifiState == STATE_WIFI_AP) {
            int clients = WiFi.softAPgetStationNum();
            dotColor = pokoClrErr();
            showDot = (clients > 0) ? true : blinkPhase;
        }

        if (showDot) {
            _statusCanvas->fillCircle(7, 6, 2, dotColor);
        }

        // Audio status indicator dot (Cyan=SSync, Pink=Music, Blue=Video, Red=Error, Blinking=Playing, Solid=Paused/Idle)
        if (audioManager) {
            audioManager->drawStatusDot(_statusCanvas, 15, 6, 2);
        }

        // Center: "PoKo" branding (exact horizontal center: x=64)
        _statusCanvas->setFont(u8g2_font_helvB08_tf);
        _statusCanvas->setTextColor(theme.headerText, theme.headerBg);
        int16_t x1, y1; uint16_t w, h;
        _statusCanvas->getTextBounds("PoKo", 0, 0, &x1, &y1, &w, &h);
        _statusCanvas->setCursor(64 - w / 2, 10);
        _statusCanvas->print("PoKo");

        // Right: Live time (or uptime until NTP time is synced)
        char buf[12];
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        static bool s_timeEverSynced = false;
        if (timeinfo.tm_year > (2020 - 1900)) {
            s_timeEverSynced = true;
        }

        if (s_timeEverSynced) {
            strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        } else {
            uint32_t up = millis() / 1000;
            if (up < 3600) snprintf(buf, sizeof(buf), "%lus", (unsigned long)up);
            else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(up / 60));
        }

        _statusCanvas->setFont(u8g2_font_profont10_mf);
        _statusCanvas->setTextColor(theme.muted, theme.headerBg);
        _statusCanvas->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        int16_t timeX = 126 - w;
        _statusCanvas->setCursor(timeX, 9);
        _statusCanvas->print(buf);

        // Battery indicator icon - positioned right next to the time (only if battery is connected)
        if (batteryManager.isPresent()) {
            int batPct = batteryManager.getPercentage();
            bool batChg = batteryManager.isCharging();
            drawBatteryIcon(timeX - 15, 3, batPct, batChg);
        }

        _statusCanvas->flush();
    }

void PokoUI::drawBatteryIcon(int x, int y, int pct, bool charging) {
        const auto& theme = currentTheme();
        pct = constrain(pct, 0, 100);

        uint16_t bodyColor;
        uint16_t fillColor;

        if (charging) {
            // Charging: Blink entire battery icon in green (bright in dark, dark in light)
            bool chgBlink = ((millis() / 450) % 2 == 0);
            if (chgBlink) {
                bodyColor = pokoClrGreen();
                fillColor = pokoClrGreen();
            } else {
                bodyColor = isDarkTheme() ? pokoGfx->color565(20, 80, 30) : pokoGfx->color565(160, 210, 160);
                fillColor = isDarkTheme() ? pokoGfx->color565(10, 50, 20) : pokoGfx->color565(190, 235, 190);
            }
        } else {
            // Discharging: Smooth color transition from Green -> Yellow -> Red as percentage drops
            uint8_t r = 0, g = 0, b = 0;
            if (pct >= 50) {
                // 50%..100%: Yellow (255, 210, 0) -> Green (0, 255, 50)
                float t = (pct - 50) / 50.0f;
                r = (uint8_t)(255 * (1.0f - t));
                g = 255;
                b = (uint8_t)(50 * t);
            } else {
                // 0%..50%: Red (255, 30, 30) -> Yellow (255, 210, 0)
                float t = pct / 50.0f;
                r = 255;
                g = (uint8_t)(210 * t + 30 * (1.0f - t));
                b = (uint8_t)(30 * (1.0f - t));
            }
            if (!isDarkTheme()) {
                // In light theme, darken slightly so fill is bold and distinct on white/light-grey
                r = (uint8_t)(r * 0.65f);
                g = (uint8_t)(g * 0.65f);
                b = (uint8_t)(b * 0.65f);
            }
            fillColor = pokoGfx->color565(r, g, b);

            if (pct <= 15) {
                // Low battery: critical red shell, blink if very low
                bool lowBlink = ((millis() / 400) % 2 == 0);
                bodyColor = lowBlink ? pokoClrErr() : theme.muted;
            } else {
                bodyColor = theme.muted;
            }
        }

        // Battery outer shell (11x6 px) + nipple (1x2 px)
        _statusCanvas->drawRect(x, y, 11, 6, bodyColor);
        _statusCanvas->drawFastVLine(x + 11, y + 2, 2, bodyColor);

        // Fill bar (0 to 7 px width inside, height 2 px)
        int fillW = map(pct, 0, 100, 0, 7);
        if (pct > 0 && fillW == 0) fillW = 1;
        if (fillW > 0) {
            _statusCanvas->fillRect(x + 2, y + 2, fillW, 2, fillColor);
        }

        // Extra charging bolt indicator in center when charging
        if (charging) {
            _statusCanvas->drawPixel(x + 5, y + 2, isDarkTheme() ? RGB565_WHITE : RGB565_BLACK);
        }
    }

void PokoUI::drawAppIcon(int cx, int cy, AppState state, uint16_t color, uint16_t bg) {
        switch (state) {
        case STATE_INFO:
            // ── i ── circle + dot + bar
            _gfx->drawCircle(cx, cy, 11, color);
            _gfx->fillCircle(cx, cy - 4, 2, color);
            _gfx->fillRect(cx - 1, cy - 1, 3, 8, color);
            break;
        case STATE_CLOCK:
            // ── clock face + two hands
            _gfx->drawCircle(cx, cy, 11, color);
            _gfx->drawLine(cx, cy, cx, cy - 7, color);
            _gfx->drawLine(cx, cy, cx + 5, cy + 2, color);
            _gfx->fillCircle(cx, cy, 2, color);
            break;
        case STATE_SSYNC:
            // ── speaker + two wave arcs
            _gfx->fillRect(cx - 9, cy - 4, 6, 9, color);
            _gfx->fillTriangle(cx - 3, cy - 6, cx - 3, cy + 7, cx + 5, cy, color);
            _gfx->drawCircle(cx + 8, cy, 5, color);
            _gfx->drawCircle(cx + 8, cy, 9, color);
            break;
        case STATE_MUSIC_UI:
            // ── double beamed music note
            _gfx->fillCircle(cx - 4, cy + 5, 4, color);
            _gfx->fillCircle(cx + 4, cy + 3, 4, color);
            _gfx->drawFastVLine(cx,     cy - 6, 11, color);
            _gfx->drawFastVLine(cx + 8, cy - 8, 11, color);
            _gfx->drawLine(cx, cy - 6, cx + 8, cy - 8, color);
            break;
        case STATE_VIDEO_UI:
            // ── play triangle
            _gfx->fillTriangle(cx - 8, cy - 10, cx - 8, cy + 10, cx + 9, cy, color);
            break;
        case STATE_GALLERY_UI:
            // ── image frame + mountain + sun
            _gfx->drawRoundRect(cx - 10, cy - 8, 20, 16, 2, color);
            _gfx->fillCircle(cx - 4, cy - 3, 2, color);
            _gfx->fillTriangle(cx - 9, cy + 7, cx + 9, cy + 7, cx, cy - 1, color);
            break;
        case STATE_PIXELS_UI:
            // ── 8-LED ring around center core ──
            for (int a = 0; a < 8; a++) {
                float angle = a * (6.2831853f / 8.0f) - 1.5707963f;
                int dx = cx + (int)(cosf(angle) * 9.5f + 0.5f);
                int dy = cy + (int)(sinf(angle) * 9.5f + 0.5f);
                _gfx->fillCircle(dx, dy, 2, color);
            }
            _gfx->fillCircle(cx, cy, 3, color);
            break;
        case STATE_SETTINGS_UI:
            // ── 4-tooth gear
            _gfx->fillRect(cx - 2, cy - 13, 4, 5, color);
            _gfx->fillRect(cx - 2, cy + 8,  4, 5, color);
            _gfx->fillRect(cx - 13, cy - 2, 5, 4, color);
            _gfx->fillRect(cx + 8,  cy - 2, 5, 4, color);
            _gfx->fillCircle(cx, cy, 9, color);
            _gfx->fillCircle(cx, cy, 4, bg);
            break;
        default:
            _gfx->fillCircle(cx, cy, 8, color);
            break;
        }
    }

void PokoUI::drawTile(uint8_t idx) {
        const PokoTile& t = _tiles[idx];
        const auto& theme = currentTheme();
        uint16_t accentCol = getTileAccentColor(t.state);

        // Clear main carousel area with theme background
        _gfx->fillRect(0, 13, 128, 101, theme.bg);

        // Carousel chevrons
        _gfx->setFont(u8g2_font_helvB10_tf);
        _gfx->setTextColor(theme.line, theme.bg);
        _gfx->setCursor(4, 52);
        _gfx->print("<");
        _gfx->setCursor(118, 52);
        _gfx->print(">");

        // Rounded box border + fill
        _gfx->drawRoundRect(36, 26, 56, 42, 8, accentCol);
        _gfx->fillRoundRect(38, 28, 52, 38, 6, theme.surface);

        // Draw the per-app icon centred in the box (box centre: 64, 47)
        drawAppIcon(64, 47, t.state, accentCol, theme.surface);

        int16_t x1, y1; uint16_t w, h;

        // App Name
        _gfx->setFont(u8g2_font_helvB10_tf);
        _gfx->setTextColor(theme.text, theme.bg);
        _gfx->getTextBounds(t.name, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 82);
        _gfx->print(t.name);

        // Subtitle
        if (t.subtitle && strlen(t.subtitle) > 0) {
            _gfx->setFont(u8g2_font_profont10_mf);
            _gfx->setTextColor(theme.muted, theme.bg);
            _gfx->getTextBounds(t.subtitle, 0, 0, &x1, &y1, &w, &h);
            _gfx->setCursor(64 - w / 2, 95);
            _gfx->print(t.subtitle);
        }

        // Footer Navigation Bar (y=114..127)
        _gfx->fillRect(0, 114, 128, 14, theme.headerBg);
        _gfx->drawFastHLine(0, 114, 128, theme.line);
        _gfx->setFont(u8g2_font_5x7_tf);
        _gfx->setTextColor(theme.footerText, theme.headerBg);
        const char* hint = "L:Prv  R:Nxt  2R:Open";
        _gfx->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        _gfx->setCursor(64 - w / 2, 124);
        _gfx->print(hint);
    }

void PokoUI::drawNavIndicator() {
        const auto& theme = currentTheme();
        _gfx->fillRect(0, 102, 128, 11, theme.bg);
        int totalW = TILE_COUNT * 8 + (TILE_COUNT - 1) * 4;
        int startX = (128 - totalW) / 2;
        for (uint8_t i = 0; i < TILE_COUNT; i++) {
            int x = startX + i * 12;
            if (i == _selected) {
                _gfx->fillRoundRect(x, 105, 8, 4, 2, theme.accent);
            } else {
                _gfx->fillCircle(x + 4, 107, 2, theme.line);
            }
        }
    }

PokoUI::PokoUI(Arduino_GFX* gfx, AppSwitchFn switchFn)
        : _gfx(gfx), _switchApp(switchFn) {}

void PokoUI::begin() {
        _statusCanvas = new (std::nothrow) Arduino_Canvas(128, 13, _gfx, 0, 0);
        if (_statusCanvas) _statusCanvas->begin();
        _dirty = true;
    }

void PokoUI::redraw() { _dirty = true; }

void PokoUI::updateStatusBar() {
        drawStatusBar();
    }

void PokoUI::renderDirect() {
        _dirty = false;
        drawStatusBar();
        drawTile(_selected);
        drawNavIndicator();
    }

void PokoUI::setTileSubtitle(AppState s, const char* sub) {
        int idx = stateToTile(s);
        if (idx < 0 || idx >= TILE_COUNT) return;
        _tiles[idx].subtitle = sub;
        if (_selected == (uint8_t)idx) _dirty = true;
    }

void PokoUI::flashHighlight() {
        uint16_t flashColor = isDarkTheme() ? RGB565_WHITE : RGB565_BLACK;
        _gfx->drawRoundRect(34, 24, 60, 46, 10, flashColor);
        delay(40);
        _gfx->drawRoundRect(34, 24, 60, 46, 10, currentTheme().bg);
        uint16_t accentCol = getTileAccentColor(_tiles[_selected].state);
        _gfx->drawRoundRect(36, 26, 56, 42, 8, accentCol);
    }

void PokoUI::navigateLeft() {
        _selected = (_selected == 0) ? (TILE_COUNT - 1) : (_selected - 1);
        flashHighlight();
        _dirty = true;
    }

void PokoUI::navigateRight() {
        _selected = (_selected + 1) % TILE_COUNT;
        flashHighlight();
        _dirty = true;
    }

void PokoUI::enter() {
        if (_switchApp) _switchApp(_tiles[_selected].state);
    }

uint8_t PokoUI::selectedIndex() const { return _selected; }

AppState PokoUI::selectedState() const { return _tiles[_selected].state; }

void PokoUI::update() {
        uint32_t now = millis();
        static bool lastChg = false;
        bool curChg = batteryManager.isCharging();
        if (curChg != lastChg) {
            lastChg = curChg;
            _lastStatusMs = now;
            drawStatusBar();
        }

        bool audioBlinking = (audioManager && (audioManager->isSoundPlaying() || audioManager->hasError()));
        bool wifiBlinking = (wifiState != STATE_WIFI_CONNECTED);
        bool batteryBlinking = batteryManager.isPresent() && (batteryManager.isCharging() || (batteryManager.getPercentage() <= 15));
        uint32_t updateInterval = (audioBlinking || wifiBlinking || batteryBlinking) ? 200 : 1000;
        if (now - _lastStatusMs >= updateInterval) {
            _lastStatusMs = now;
            drawStatusBar();
        }
        if (_dirty) {
            _dirty = false;
            drawStatusBar();
            drawTile(_selected);
            drawNavIndicator();
        }
    }

uint16_t getTileAccentColor(AppState state) {
    if (isDarkTheme()) {
        switch (state) {
            case STATE_INFO:        return 0x07E0; // Bright Green
            case STATE_CLOCK:       return 0x07FF; // Bright Cyan
            case STATE_SSYNC:       return 0x07E0; // Bright Green
            case STATE_MUSIC_UI:    return 0xF81F; // Bright Pink
            case STATE_VIDEO_UI:    return 0x001F; // Blue
            case STATE_GALLERY_UI:  return 0xFD20; // Orange
            case STATE_PIXELS_UI:   return 0xFBE0; // Yellow
            case STATE_SETTINGS_UI: return 0x8410; // Mid Grey
            default:                return 0x07FF;
        }
    } else {
        // Light Theme: Deep high-contrast saturated tones that pop on light surface & white bg
        switch (state) {
            case STATE_INFO:        return 0x0400; // Dark Forest Green
            case STATE_CLOCK:       return 0x01F4; // Deep Royal Navy
            case STATE_SSYNC:       return 0x0400; // Dark Forest Green
            case STATE_MUSIC_UI:    return 0x90B0; // Deep Plum
            case STATE_VIDEO_UI:    return 0x0115; // Deep Navy
            case STATE_GALLERY_UI:  return 0xCA20; // Burnt Orange
            case STATE_PIXELS_UI:   return 0xB360; // Dark Amber / Goldenrod
            case STATE_SETTINGS_UI: return 0x4228; // Dark Charcoal
            default:                return 0x01F4;
        }
    }
}
