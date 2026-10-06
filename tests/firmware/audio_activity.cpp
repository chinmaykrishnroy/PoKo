#include <cstdint>
#include <iostream>
#include <string>
#include "AudioActivity.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed: " #x << '\n'; return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    if (test == "silence") {
        const uint8_t pcm[] = {0, 0, 12, 0, 0xff, 0xff, 96, 0};
        CHECK(!pcm16HasAudibleSignal(pcm, sizeof(pcm)));
    } else if (test == "audible-positive") {
        const uint8_t pcm[] = {97, 0, 0, 0};
        CHECK(pcm16HasAudibleSignal(pcm, sizeof(pcm)));
    } else if (test == "audible-negative") {
        const uint8_t pcm[] = {0x9f, 0xff};
        CHECK(pcm16HasAudibleSignal(pcm, sizeof(pcm)));
    } else {
        return 2;
    }
    return 0;
}
