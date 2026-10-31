#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <functional>
#include "../../PokoPins.h"
constexpr int LOW=0,HIGH=1,INPUT_PULLUP=2,WIFI_OFF=0,ESP_EXT1_WAKEUP_ANY_LOW=0,ESP_SLEEP_WAKEUP_ALL=0;
using gpio_num_t=int;
uint32_t clockMs=0, releaseAt[64]={}, sleepAt=0;
uint64_t wakeMask=0;
int watchdogFeeds=0, delays=0;
bool latchOff=false, asleep=false, wakeSourcesCleared=false;
bool audioStopped=false,codecOff=false,preferencesFlushed=false;
bool rtcMode[64]={}, rtcHeld[64]={}, pullup[64]={};
uint32_t millis() { return clockMs; }
int digitalRead(int pin) { return !rtcMode[pin] && clockMs < releaseAt[pin] ? LOW : HIGH; }
void delay(int ms) { clockMs+=ms;++delays;if(clockMs>20000)throw std::runtime_error("shutdown never completed"); }
void pinMode(int pin,int mode) { pullup[pin]=mode==INPUT_PULLUP; }
void rtc_gpio_hold_dis(int pin) { rtcHeld[pin]=false; }
void rtc_gpio_deinit(int pin) { rtcMode[pin]=false; }
void esp_task_wdt_reset() { ++watchdogFeeds; }
void rtc_gpio_pullup_en(int) {}
void rtc_gpio_pulldown_dis(int) {}
void esp_sleep_disable_wakeup_source(int) { wakeSourcesCleared=true;wakeMask=0; }
void esp_sleep_enable_ext1_wakeup(uint64_t mask,int) { wakeMask=mask; }
void esp_deep_sleep_start() { asleep=true;sleepAt=clockMs; }
void powerLatchOff() { latchOff=true; }
void powerLatchOn() {}
bool completePendingIdlePowerOffWake() { return false; }
void shutdownAudioOutputHardware() { codecOff=true; }
void turnOffLEDs() {}
void setBacklight(int) {}
void displaySleep() {}
struct { template<class... T> void printf(const char*,T...){} void println(const char*){} } Serial;
struct { void restart(){} } ESP;
struct { void disconnect(bool){} void mode(int){} } WiFi;
struct Preferences { void begin(const char*,bool){} void putBool(const char*,bool){} void end(){} };
struct Audio { void flushVolume(){} void stopAll(){audioStopped=true;} } audio;
struct PowerManager { std::function<void()> _powerDownCb;Audio* _audio=&audio;int _targetDuty=0,_currentDuty=0;void powerOff(bool); };
#include "../../build/power-wake-method.inc"
int main(int argc,char** argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"rtc-restore-before-boot")) {
        releaseAt[POKO_PIN_BTN_PWR]=5000;
        for(int pin:{POKO_PIN_BTN_PWR,POKO_PIN_BTN_DOWN,POKO_PIN_BTN_UP}) rtcMode[pin]=rtcHeld[pin]=true;
        bootUntilSerial();
        for(int pin:{POKO_PIN_BTN_PWR,POKO_PIN_BTN_DOWN,POKO_PIN_BTN_UP})
            if(rtcMode[pin] || rtcHeld[pin] || !pullup[pin]) { puts("FAIL: boot samples buttons before restoring RTC GPIOs");return 1; }
        assert(digitalRead(POKO_PIN_BTN_PWR)==LOW);
        puts("PASS");return 0;
    }
    if(!strcmp(argv[1],"held-power")) releaseAt[POKO_PIN_BTN_PWR]=5000;
    else if(!strcmp(argv[1],"held-combo")) { releaseAt[POKO_PIN_BTN_PWR]=5000;releaseAt[POKO_PIN_BTN_DOWN]=7000; }
    else if(!strcmp(argv[1],"released")) releaseAt[POKO_PIN_BTN_PWR]=500;
    else return 2;
    PowerManager manager;manager._powerDownCb=[](){assert(audioStopped && codecOff);preferencesFlushed=true;};manager.powerOff(false);
    const uint64_t all=(1ULL<<POKO_PIN_BTN_PWR)|(1ULL<<POKO_PIN_BTN_DOWN)|(1ULL<<POKO_PIN_BTN_UP);
    if(wakeMask!=all) { puts("FAIL: held shutdown gesture permanently removes a wake button");return 1; }
    assert(latchOff && asleep && preferencesFlushed);
    for(int pin:{POKO_PIN_BTN_PWR,POKO_PIN_BTN_DOWN,POKO_PIN_BTN_UP}) assert(sleepAt>=releaseAt[pin]);
    assert(watchdogFeeds>0 && delays>0);
    assert(wakeSourcesCleared);
    puts("PASS");
}
