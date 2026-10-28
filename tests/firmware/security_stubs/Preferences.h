#pragma once
#include "WebServer.h"
class Preferences {
public:
    std::map<std::string, String> values;
    std::map<std::string, uint64_t> integers;
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
    bool remove(const char* key) { values.erase(key); integers.erase(key); return true; }
    uint64_t getULong64(const char* key, uint64_t fallback) { auto i=integers.find(key); return i==integers.end()?fallback:i->second; }
    size_t putULong64(const char* key, uint64_t value) { if(failWrite)return 0; integers[key]=value; return sizeof(uint64_t); }
};
