#include "MicrophoneInput.h"
#include "PokoDrivers.h"
#include "PokoPins.h"
#include <Wire.h>
#include <algorithm>

MicrophoneInput microphoneInput;
MicrophoneInput::MicrophoneInput() : _rx(nullptr),_gain(18),_overruns(0) {}
bool MicrophoneInput::running() const { return _rx!=nullptr; }
int MicrophoneInput::gain() const { return _gain; }
uint32_t MicrophoneInput::overruns() const { return _overruns.load(std::memory_order_relaxed); }
bool IRAM_ATTR MicrophoneInput::onOverflow(i2s_chan_handle_t, i2s_event_data_t*, void* context) {
    static_cast<MicrophoneInput*>(context)->_overruns.fetch_add(1,std::memory_order_relaxed);
    return false;
}
bool MicrophoneInput::writeReg(uint8_t reg,uint8_t value) {
    Wire.beginTransmission(0x40);Wire.write(reg);Wire.write(value);
    return Wire.endTransmission()==0;
}
bool MicrophoneInput::setGain(int db) {
    db=std::max(0,std::min(30,db));db=(db/3)*3;
    if(_rx && (!writeReg(0x43,0x10+db/3) || !writeReg(0x44,0x10+db/3))) return false;
    _gain=db;return true;
}
bool MicrophoneInput::configure() {
    // ES7210 reset/power/clock sequence follows Espressif esp_codec_dev
    // (Apache-2.0), device/es7210/es7210.c. Only MIC1/2 are selected, standard
    // 16-bit stereo I2S, 48 kHz at 12.288 MHz MCLK (256 Fs), ADC clock 0xc1.
    const uint8_t sequence[][2]={
        {0x00,0xff},{0x00,0x41},{0x01,0x3f},{0x09,0x30},{0x0a,0x30},
        {0x23,0x2a},{0x22,0x0a},{0x20,0x0a},{0x21,0x2a},{0x08,0x00},
        {0x40,0x43},{0x41,0x70},{0x42,0x70},{0x07,0x20},{0x02,0xc1},
        {0x04,0x01},{0x05,0x00},{0x11,0x60},{0x12,0x00},
        {0x45,0x00},{0x46,0x00},{0x4b,0x00},{0x4c,0xff},
        {0x01,0x34},{0x06,0x00},{0x47,0x08},{0x48,0x08},
        {0x49,0x08},{0x4a,0x08},{0x14,0x00},{0x15,0x00},
        {0x00,0x71},{0x00,0x41}
    };
    for(const auto& item:sequence) if(!writeReg(item[0],item[1])) return false;
    return writeReg(0x43,0x10+_gain/3) && writeReg(0x44,0x10+_gain/3);
}
bool MicrophoneInput::start() {
    if(_rx) return true;
    // Never steal clocks from an output worker whose handoff did not finish.
    if(poko_tx_handle) return false;
    shutdownAudioOutputHardware();
    Wire.begin(POKO_PIN_I2C_SDA,POKO_PIN_I2C_SCL);
    Wire.setTimeOut(50);
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    // 171 ms of RX headroom covers a DSP pass plus a short HTTP/Wi-Fi stall.
    channel.dma_desc_num=16;channel.dma_frame_num=512;
    if(i2s_new_channel(&channel,nullptr,&_rx)!=ESP_OK) { _rx=nullptr;return false; }
    i2s_std_config_t config={};
    config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    config.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
    config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk=(gpio_num_t)POKO_PIN_I2S_MCLK;
    config.gpio_cfg.bclk=(gpio_num_t)POKO_PIN_I2S_BCLK;
    config.gpio_cfg.ws=(gpio_num_t)POKO_PIN_I2S_LRC;
    config.gpio_cfg.dout=(gpio_num_t)I2S_GPIO_UNUSED;
    config.gpio_cfg.din=(gpio_num_t)POKO_PIN_I2S_DIN;
    _overruns.store(0,std::memory_order_relaxed);
    i2s_event_callbacks_t callbacks={};callbacks.on_recv_q_ovf=onOverflow;
    if(i2s_channel_init_std_mode(_rx,&config)!=ESP_OK ||
       i2s_channel_register_event_callback(_rx,&callbacks,this)!=ESP_OK ||
       i2s_channel_enable(_rx)!=ESP_OK || !configure()) {
        stop();Serial.println("[mic] ES7210/I2S initialization failed");return false;
    }
    setSpeakerAmp(false);
    Serial.println("[mic] ES7210 MIC1/2 ready, 48000 Hz; speaker PA off");
    return true;
}
void MicrophoneInput::stop() {
    if(!_rx) return;
    for(uint8_t reg=0x47;reg<=0x4c;++reg) writeReg(reg,0xff);
    writeReg(0x40,0xc0);writeReg(0x01,0x7f);writeReg(0x06,0x07);
    i2s_channel_disable(_rx);i2s_del_channel(_rx);_rx=nullptr;
    setSpeakerAmp(false);
}
size_t MicrophoneInput::read(int16_t* mono,size_t frames) {
    if(!_rx || !frames) return 0;
    int16_t stereo[512*2];size_t bytes=0;
    frames=std::min<size_t>(frames,512);
    i2s_channel_read(_rx,stereo,frames*4,&bytes,0); // Never wait in the UI/service loop.
    size_t received=bytes/4;
    for(size_t i=0;i<received;++i) mono[i]=stereo[2*i]; // MIC1; avoid dual-mic phase comb filtering.
    return received;
}
