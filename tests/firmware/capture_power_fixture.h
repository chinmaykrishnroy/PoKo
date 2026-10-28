#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "../../PowerPolicy.h"
enum AudioSource {AUDIO_NONE,AUDIO_SSYNC,AUDIO_VIDEO,AUDIO_MIC};
constexpr int POWER_LOCK_AUDIO=1,POWER_LOCK_REALTIME_NET=2,POWER_LOCK_OTA=4,LOCK_OWNER_AUDIO=1;
int locks=0,restores=0,shutdowns=0;
bool amp=false;
void* poko_tx_handle=nullptr;
bool isSpeakerAmpEnabled() { return amp; }
void setSpeakerAmp(bool enabled) { amp=enabled; }
void restoreAudioOutputHardware() { ++restores;amp=true; }
void standbyAudioOutputHardware() { amp=false; }
void shutdownAudioOutputHardware() { ++shutdowns;amp=false; }
void acquireLock(int bit,int) { locks|=bit; }
void releaseLock(int bit,int) { locks&=~bit; }
struct AudioManager {
    AudioSource source=AUDIO_MIC;
    bool audible=false;
    AudioSource activeSource() const { return source; }
    bool isSoundPlaying() const { return audible; }
    bool hasActiveSession() const { return source!=AUDIO_NONE; }
} audio;
struct { void println(const char*) {} } Serial;
