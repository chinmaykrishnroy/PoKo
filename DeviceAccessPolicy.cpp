#include "DeviceAccessPolicy.h"
#include <cstring>

bool deviceControlRoute(const char* path, bool get) {
    if (!get) return false;
    const char* allowed[] = {"/api/health", "/api/app", "/api/power", "/api/sys",
        "/api/gallery/files", "/api/ui/playback_stopped", "/api/audio/stop"};
    for (const char* candidate : allowed)
        if (std::strcmp(path, candidate) == 0) return true;
    return false;
}
