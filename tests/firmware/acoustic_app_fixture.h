#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "PokoTheme.h"
inline const PokoThemePalette palette={};
inline const PokoThemePalette& currentTheme() { return palette; }
inline uint16_t pokoClrErr() { return 1; }
inline uint16_t pokoClrGreen() { return 2; }
enum AudioSource { AUDIO_NONE,AUDIO_MIC };
struct MicrophoneInput {
    bool active=false,noData=false,failGain=false;
    int reads=0,sample=0;
    bool running() const { return active; }
    bool setGain(int) { return !failGain; }
    size_t read(int16_t* out,size_t n) {
        ++reads;if(noData) return 0;
        for(size_t i=0;i<n;++i) out[i]=(int16_t)(sin(6.28318530718*440*(sample++)/48000)*16384);
        return n;
    }
} microphoneInput;
struct AudioManager {
    bool fail=false;
    int requests=0,releases=0;
    AudioSource source=AUDIO_NONE;
    bool request(AudioSource s) { ++requests;if(fail) return false;source=s;microphoneInput.active=true;return true; }
    void release(AudioSource s) { if(source==s) { ++releases;source=AUDIO_NONE;microphoneInput.active=false; } }
    AudioSource activeSource() const { return source; }
} manager;
inline AudioManager* audioManager=&manager;
