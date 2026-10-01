#include "PokoAppState.h"

int stateToTile(AppState state) {
    switch (state) {
        case STATE_INFO: return 0;
        case STATE_CLOCK: return 1;
        case STATE_SSYNC: return 2;
        case STATE_MUSIC_UI: return 3;
        case STATE_VIDEO_UI: return 4;
        case STATE_GALLERY_UI: return 5;
        case STATE_PIXELS_UI: return 6;
        case STATE_SETTINGS_UI: return 7;
        default: return -1;
    }
}

String escapeJson(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}
