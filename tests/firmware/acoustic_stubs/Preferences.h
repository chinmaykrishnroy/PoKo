#pragma once
#include <map>
#include <string>
#include <cstdint>
inline std::map<std::string,int> storedPrefs;
inline int preferenceWrites=0;
class Preferences {
    std::string _namespace;
public:
    bool begin(const char* name,bool) { _namespace=name;return true; }
    void end() {}
    // Xtensa's int32_t is long, distinct from int. Preserve that distinction on
    // host platforms so template deduction errors cannot hide behind the stub.
    long getInt(const char* key,int fallback) {
        auto it=storedPrefs.find(_namespace+key);return it==storedPrefs.end()?fallback:it->second;
    }
    uint8_t getUChar(const char* key,uint8_t fallback) { return (uint8_t)getInt(key,fallback); }
    void putInt(const char* key,int value) { storedPrefs[_namespace+key]=value;++preferenceWrites; }
    void putUChar(const char* key,uint8_t value) { putInt(key,value); }
};
