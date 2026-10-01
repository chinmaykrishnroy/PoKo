#include "PokoTheme.h"

const PokoThemePalette POKO_THEME_DARK = {
    0x0000, 0x1082, 0x18C3, 0xFFFF, 0x8410, 0x2945,
    0x0841, 0x0841, 0xFFFF, 0x9CD3, 0x07FF
};
const PokoThemePalette POKO_THEME_LIGHT = {
    0xFFFF, 0xF7BE, 0xDEFB, 0x0000, 0x632C, 0xCE79,
    0xDEFB, 0xDEFB, 0x0000, 0x4208, 0x01F4
};

namespace { bool darkTheme = true; }
const PokoThemePalette& currentTheme() { return darkTheme ? POKO_THEME_DARK : POKO_THEME_LIGHT; }
void setPokoTheme(bool dark) { darkTheme = dark; }
bool isDarkTheme() { return darkTheme; }

uint16_t pokoClrGreen() { return isDarkTheme() ? 0x07E0 : 0x0400; }
uint16_t pokoClrWarn() { return isDarkTheme() ? 0xFD20 : 0xB360; }
uint16_t pokoClrCyan() { return isDarkTheme() ? 0x07FF : 0x01F4; }
uint16_t pokoClrErr() { return isDarkTheme() ? 0xF800 : 0xB000; }
