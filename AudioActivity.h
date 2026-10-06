#pragma once

#include <stddef.h>
#include <stdint.h>

bool pcm16HasAudibleSignal(const uint8_t* data, size_t bytes,
                           int16_t threshold = 96);
