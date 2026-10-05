#pragma once

// ─────────────────────────────────────────────────────────────
//  CoreSystemContext — Dependency injection container
//
//  A single SystemContext is assembled in setup() and passed to
//  every subsystem that needs cross-component access. This eliminates
//  the 12+ extern globals previously declared in PokoAPI.h, making
//  each subsystem's dependencies explicit and testable.
//
//  Forward-declarations keep this header lightweight — nothing here
//  pulls in Arduino, ESP-IDF, or any app-specific header.
// ─────────────────────────────────────────────────────────────

// Forward declarations only — no includes
class AudioManager;
class PowerManager;
class AppManager;
class BatteryManager;
class SnapPlayer;
class PixelEngine;
class PokoUI;
class SSyncApp;
class MusicApp;
class VideoApp;
class GalleryApp;

struct SystemContext {
    AppManager&     apps;
    AudioManager&   audio;
    PowerManager&   power;
    BatteryManager& battery;
    SnapPlayer&     snap;
    PixelEngine&    pixels;
    // nullable — used by API endpoints that need app-specific state
    SSyncApp*       ssync    = nullptr;
    MusicApp*       music    = nullptr;
    VideoApp*       video    = nullptr;
    GalleryApp*     gallery  = nullptr;
};
