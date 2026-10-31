#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "PokoTheme.h"
inline const PokoThemePalette palette={0,0x1082,0x18E3,0xFFFF,0x8410,0x4208,0x1082,0x1082,0xFFFF,0x8410,0x07FF};
inline const PokoThemePalette& currentTheme() { return palette; }
inline uint16_t pokoClrErr() { return 0xF800; }
inline uint16_t pokoClrGreen() { return 0x07E0; }
inline uint16_t pokoClrCyan() { return 0x07FF; }
inline bool isDarkTheme() { return true; }
enum AudioSource { AUDIO_NONE,AUDIO_SSYNC,AUDIO_MUSIC,AUDIO_VIDEO,AUDIO_BLUETOOTH,AUDIO_MIC };
inline bool speakerAmp=false;
inline bool isSpeakerAmpEnabled() { return speakerAmp; }
struct MicrophoneInput {
    bool active=false,noData=false,failGain=false,quiet=false;
    bool chord=false;
    float frequency=440;
    float amplitude=.5f;
    int gainDb=18;
    uint32_t overflowCount=0;
    int overflowOnRead=-1;
    uint32_t overruns() const { return overflowCount; }
    int reads=0,sample=0;
    bool running() const { return active; }
    bool setGain(int value) { if(failGain) return false;gainDb=value;return true; }
    size_t read(int16_t* out,size_t n) {
        ++reads;if(reads==overflowOnRead) ++overflowCount;if(noData) return 0;
        for(size_t i=0;i<n;++i) {
            double phase=6.28318530718*(sample++)/48000;
            double value=chord?(sin(261.625565*phase)+sin(329.627557*phase)+sin(391.995436*phase))/3:sin(frequency*phase);
            out[i]=quiet?0:(int16_t)(value*32768*amplitude);
        }
        return n;
    }
} microphoneInput;
struct AudioManager {
    bool fail=false,playing=false,incoming=false;
    int requests=0,releases=0;
    AudioSource source=AUDIO_NONE;
    bool request(AudioSource s) { ++requests;if(fail) return false;source=s;microphoneInput.active=true;return true; }
    void release(AudioSource s) { if(source==s) { ++releases;source=AUDIO_NONE;microphoneInput.active=false; } }
    AudioSource activeSource() const { return source; }
    bool isSoundPlaying() const { return playing; }
    bool resumeIncomingPlayback(uint32_t) { if(!incoming || source!=AUDIO_MIC) return false;incoming=false;source=AUDIO_SSYNC;microphoneInput.active=false;return true; }
} manager;
inline AudioManager* audioManager=&manager;
