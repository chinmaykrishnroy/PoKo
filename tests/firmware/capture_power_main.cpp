int main(int argc,char** argv) {
    assert(argc==2);PowerHarness p;
    if(!strcmp(argv[1],"capture-pa-off-and-lock")) {
        // Expired idle hardware timers and a previous realtime lock must never
        // restore the output PA or shut down I2S beneath microphone capture.
        locks=POWER_LOCK_REALTIME_NET;amp=true;
        for(uint32_t now=10000;now<86400000;now+=10000) {
            p.update(now);
            assert(!amp && !restores && !shutdowns);
            assert((locks&POWER_LOCK_AUDIO) && !(locks&POWER_LOCK_REALTIME_NET));
            assert(p.recommendedLoopYieldMs()==1);
            assert(!shouldAutoPowerOff(10,5,80,true,false,true,false,now));
        }
        assert(shouldAutoPowerOff(10,5,5,true,false,true,false,10000));
    } else if(!strcmp(argv[1],"capture-stop-and-playback")) {
        p.update(10000);audio.source=AUDIO_NONE;p._protectedActivity=false;p.update(21000);
        // Output hardware was already off throughout RX capture; do not
        // initialize or shut it down again just because capture ended.
        assert(!(locks&POWER_LOCK_AUDIO) && shutdowns==0);
        audio.source=AUDIO_SSYNC;audio.audible=true;p.update(22000);
        assert(amp && restores==1 && (locks&POWER_LOCK_REALTIME_NET));
    } else return 2;
    puts("PASS");
}
