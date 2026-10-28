#pragma once
#include "WebServer.h"
class Preferences {
public:
    std::map<std::string, String> values;
    bool failWrite = false;
    String getString(const char* key, const char* fallback) {
        auto found = values.find(key);
        return found == values.end() ? String(fallback) : found->second;
    }
    size_t putString(const char* key, const String& value) {
        if (failWrite) return 0;
        values[key] = value;
        return value.length();
    }
};
