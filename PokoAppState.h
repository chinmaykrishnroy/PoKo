#pragma once
#include <Arduino.h>

enum AppState {
    STATE_LAUNCHER, STATE_INFO, STATE_CLOCK, STATE_VIDEO_UI, STATE_MUSIC_UI,
    STATE_SSYNC, STATE_GALLERY_UI, STATE_PIXELS_UI, STATE_SETTINGS_UI, STATE_COUNT
};

enum WifiModeState { STATE_WIFI_CONNECTING, STATE_WIFI_CONNECTED, STATE_WIFI_AP };
extern WifiModeState wifiState;
using AppSwitchFn = void (*)(AppState);

int stateToTile(AppState state);
String escapeJson(const String& value);
