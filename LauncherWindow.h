#pragma once
#include <stdint.h>

// Eight dots remain fixed on screen. Overflow scrolls the represented app
// window only when selection reaches an edge; wrap follows the selection.
uint8_t launcherWindowStart(uint8_t selected, uint8_t start,
                             uint8_t total, uint8_t visible = 8);
