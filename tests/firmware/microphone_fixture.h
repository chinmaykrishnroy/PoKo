#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
using i2s_chan_handle_t = void*;
using gpio_num_t = int;
#define IRAM_ATTR
struct i2s_event_data_t {};
struct i2s_event_callbacks_t { bool(*on_recv_q_ovf)(i2s_chan_handle_t,i2s_event_data_t*,void*)=nullptr; };
inline i2s_event_callbacks_t rxCallbacks;
inline void* rxContext=nullptr;
constexpr int ESP_OK=0,I2S_NUM_0=0,I2S_ROLE_MASTER=0,I2S_GPIO_UNUSED=-1;
constexpr int I2S_MCLK_MULTIPLE_256=256,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_STEREO=2;
constexpr int POKO_PIN_I2S_MCLK=8,POKO_PIN_I2S_BCLK=9,POKO_PIN_I2S_LRC=10,POKO_PIN_I2S_DIN=11;
constexpr int POKO_PIN_I2C_SDA=42,POKO_PIN_I2C_SCL=41;
struct i2s_chan_config_t { int dma_desc_num=0,dma_frame_num=0; };
struct Clock { int sample_rate_hz=0,mclk_multiple=256; };
struct Slot { int bits=0,mode=0; };
struct Pins { int mclk=-1,bclk=-1,ws=-1,dout=-1,din=-1; };
struct i2s_std_config_t { Clock clk_cfg;Slot slot_cfg;Pins gpio_cfg; };
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) i2s_chan_config_t{}
#define I2S_STD_CLK_DEFAULT_CONFIG(a) Clock{int(a),256}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a,b) Slot{a,b}
i2s_chan_handle_t poko_tx_handle=nullptr;
int failAt=0,allocations=0,deletions=0,enabled=0,hardwareStops=0,readWait=-1;
bool amp=true;
i2s_std_config_t capturedConfig;
int i2s_new_channel(const i2s_chan_config_t* c,void* tx,i2s_chan_handle_t* rx) {
    assert(!tx && c->dma_desc_num==16 && c->dma_frame_num==512);
    if(failAt==1) return -1;*rx=(void*)1;++allocations;return ESP_OK;
}
int i2s_channel_init_std_mode(void*,const i2s_std_config_t* c) { capturedConfig=*c;return failAt==2?-1:ESP_OK; }
int i2s_channel_register_event_callback(void*,const i2s_event_callbacks_t* callbacks,void* context) {
    rxCallbacks=*callbacks;rxContext=context;return failAt==5?-1:ESP_OK;
}
int i2s_channel_enable(void*) { if(failAt==3) return -1;++enabled;return ESP_OK; }
int i2s_channel_disable(void*) { enabled=0;return ESP_OK; }
int i2s_del_channel(void*) { ++deletions;return ESP_OK; }
int i2s_channel_read(void*,void* buffer,size_t bytes,size_t* received,int wait) {
    readWait=wait;assert(bytes<=2048);
    int16_t* samples=(int16_t*)buffer;
    for(size_t i=0;i<bytes/4;++i) { samples[i*2]=int16_t(i+100);samples[i*2+1]=-samples[i*2]; }
    *received=bytes;return ESP_OK;
}
void shutdownAudioOutputHardware() { ++hardwareStops;amp=false; }
void setSpeakerAmp(bool value) { amp=value; }
struct Bus {
    int address=0,reg=0,value=0,index=0;
    bool fail=false;
    std::map<int,int> registers;
    void begin(int sda,int scl) { assert(sda==42 && scl==41); }
    void setTimeOut(int time) { assert(time==50); }
    void beginTransmission(int a) { address=a;index=0; }
    void write(uint8_t v) { if(!index++) reg=v;else value=v; }
    int endTransmission() { assert(address==0x40);if(fail) return 2;registers[reg]=value;return 0; }
} Wire;
struct { void println(const char*) {} } Serial;
