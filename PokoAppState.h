#pragma once

// ─────────────────────────────────────────────────────────────
//  Poko — App State Enum + Shared Types
// ─────────────────────────────────────────────────────────────

enum AppState {
    STATE_LAUNCHER,       // Home carousel
    STATE_INFO,           // System info app
    STATE_CLOCK,          // Digital clock
    STATE_VIDEO_UI,       // Video player UI (over TCPStream / SyncedAVPlayer)
    STATE_MUSIC_UI,       // Music player UI (over TCPAudio backend stream)
    STATE_SSYNC,          // SSync — Snapcast client (direct app, no wrapper)
    STATE_GALLERY_UI,     // Gallery (LittleFS + backend)
    STATE_PIXELS_UI,      // NeoPixel Studio app
    STATE_SETTINGS_UI,    // On-device settings
    STATE_COUNT           // Sentinel — keep last
};

enum WifiModeState {
    STATE_WIFI_CONNECTING,
    STATE_WIFI_CONNECTED,
    STATE_WIFI_AP
};

extern WifiModeState wifiState;

// Maps a state to its tile index in the launcher carousel (order must match PokoUI _tiles[])
inline int stateToTile(AppState s) {
    switch (s) {
        case STATE_INFO:        return 0;
        case STATE_CLOCK:       return 1;
        case STATE_SSYNC:       return 2;
        case STATE_MUSIC_UI:    return 3;
        case STATE_VIDEO_UI:    return 4;
        case STATE_GALLERY_UI:  return 5;
        case STATE_PIXELS_UI:   return 6;
        case STATE_SETTINGS_UI: return 7;
        default:                return -1;
    }
}

// Callback type used by apps to request a state transition
typedef void (*AppSwitchFn)(AppState);

// Helper to safely escape characters for JSON string values
inline String escapeJson(const String& s) {
    String out = "";
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\b') {
            out += "\\b";
        } else if (c == '\f') {
            out += "\\f";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if ((uint8_t)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

