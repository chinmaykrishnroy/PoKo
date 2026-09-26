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
    STATE_SETTINGS_UI,    // On-device settings
    STATE_COUNT           // Sentinel — keep last
};

// Maps a state to its tile index in the launcher carousel (order must match PokoUI _tiles[])
inline int stateToTile(AppState s) {
    switch (s) {
        case STATE_INFO:        return 0;
        case STATE_CLOCK:       return 1;
        case STATE_SSYNC:       return 2;
        case STATE_MUSIC_UI:    return 3;
        case STATE_VIDEO_UI:    return 4;
        case STATE_GALLERY_UI:  return 5;
        case STATE_SETTINGS_UI: return 6;
        default:                return -1;
    }
}

// Callback type used by apps to request a state transition
typedef void (*AppSwitchFn)(AppState);
