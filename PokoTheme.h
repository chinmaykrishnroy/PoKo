#pragma once
#include <Arduino.h>

#define POKO_CLR_BG        0x0000
#define POKO_CLR_ACCENT    0x07FF
#define POKO_CLR_TEXT      0xFFFF
#define POKO_CLR_DIM       0x8410
#define POKO_CLR_WARN      0xFD20
#define POKO_CLR_ERR       0xF800
#define POKO_CLR_GREEN     0x07E0
#define POKO_CLR_TILE_BG   0x1082

struct PokoThemePalette {
    uint16_t bg, surface, surface2, text, muted, line;
    uint16_t headerBg, footerBg, headerText, footerText, accent;
};

extern const PokoThemePalette POKO_THEME_DARK;
extern const PokoThemePalette POKO_THEME_LIGHT;
const PokoThemePalette& currentTheme();
void setPokoTheme(bool dark);
bool isDarkTheme();

uint16_t pokoClrGreen();
uint16_t pokoClrWarn();
uint16_t pokoClrCyan();
uint16_t pokoClrErr();
