int main(int argc,char** argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"no-clock-steal")) {
        poko_tx_handle=(void*)2;assert(!microphoneInput.start());
        assert(!allocations && !hardwareStops);
    } else if(!strcmp(argv[1],"failure-cleanup")) {
        for(failAt=1;failAt<=5;++failAt) {
            Wire.fail=failAt==4;assert(!microphoneInput.start());
            assert(!microphoneInput.running() && !enabled);
            assert(allocations==deletions);
        }
    } else if(!strcmp(argv[1],"capture-and-stop")) {
        assert(microphoneInput.start() && !amp);
        assert(rxCallbacks.on_recv_q_ovf);
        assert(!rxCallbacks.on_recv_q_ovf(nullptr,nullptr,rxContext));
        assert(microphoneInput.overruns()==1);
        assert(capturedConfig.clk_cfg.sample_rate_hz==48000);
        assert(capturedConfig.clk_cfg.mclk_multiple==256);
        assert(capturedConfig.slot_cfg.bits==16 && capturedConfig.slot_cfg.mode==2);
        assert(capturedConfig.gpio_cfg.din==11 && capturedConfig.gpio_cfg.dout==-1);
        assert(Wire.registers[0x11]==0x60 && Wire.registers[0x01]==0x34);
        int16_t mono[512];assert(microphoneInput.read(mono,512)==512);
        assert(readWait==0 && mono[0]==100 && mono[511]==611);
        assert(microphoneInput.start() && allocations==1);
        microphoneInput.stop();microphoneInput.stop();
        assert(allocations==deletions && !enabled && !amp);
        assert(Wire.registers[0x4b]==0xff && Wire.registers[0x06]==0x07);
        assert(microphoneInput.start() && microphoneInput.overruns()==0);
        microphoneInput.stop();assert(allocations==deletions);
    } else if(!strcmp(argv[1],"gain")) {
        assert(microphoneInput.setGain(100) && microphoneInput.gain()==30);
        assert(microphoneInput.start());
        assert(Wire.registers[0x43]==0x1a && Wire.registers[0x44]==0x1a);
        assert(microphoneInput.setGain(7) && microphoneInput.gain()==6);
        assert(Wire.registers[0x43]==0x12);
        Wire.fail=true;assert(!microphoneInput.setGain(12));
        assert(microphoneInput.gain()==6);
        Wire.fail=false;microphoneInput.stop();
    } else return 2;
    puts("PASS");
}
