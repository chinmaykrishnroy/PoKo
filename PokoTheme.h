#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
//  PokoTheme — Theme System (Dark and Light Modes)
// ─────────────────────────────────────────────────────────────

struct PokoThemePalette {
    uint16_t bg;
    uint16_t surface;
    uint16_t surface2;
    uint16_t text;
    uint16_t muted;
    uint16_t line;
    uint16_t headerBg;
    uint16_t footerBg;
    uint16_t headerText;
    uint16_t footerText;
    uint16_t accent;
};

// Dark Mode Palette
static const PokoThemePalette POKO_THEME_DARK = {
    .bg         = 0x0000, // Black #000000
    .surface    = 0x1082, // Dark grey tile
    .surface2   = 0x18C3, // Border / frame
    .text       = 0xFFFF, // White #FFFFFF
    .muted      = 0x8410, // Medium grey
    .line       = 0x2945, // Divider line
    .headerBg   = 0x0841, // Header background
    .footerBg   = 0x0841, // Footer background
    .headerText = 0xFFFF, // Header text
    .footerText = 0x9CD3, // Footer navigation text
    .accent     = 0x07FF  // Cyan
};

// Light Mode Palette
static const PokoThemePalette POKO_THEME_LIGHT = {
    .bg         = 0xFFFF, // White #FFFFFF
    .surface    = 0xF7BE, // Light grey surface #F4F6F8
    .surface2   = 0xDEFB, // Subtle divider
    .text       = 0x0000, // Black #000000
    .muted      = 0x632C, // Slate muted #68717D
    .line       = 0xCE79, // Light line #DCE1E6
    .headerBg   = 0xDEFB, // Header background
    .footerBg   = 0xDEFB, // Footer background
    .headerText = 0x0000, // Header text
    .footerText = 0x4208, // Footer navigation text
    .accent     = 0x01F4  // Deep Royal Navy / Teal
};

inline bool pokoIsDarkTheme = true;

inline const PokoThemePalette& currentTheme() {
    return pokoIsDarkTheme ? POKO_THEME_DARK : POKO_THEME_LIGHT;
}

inline void setPokoTheme(bool dark) {
    pokoIsDarkTheme = dark;
}

inline bool isDarkTheme() {
    return pokoIsDarkTheme;
}
