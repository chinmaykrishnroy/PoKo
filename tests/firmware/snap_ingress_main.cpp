int main(int argc,char**argv) {
    assert(argc==2);SnapPlayer snap;int16_t pcm[64];for(auto&v:pcm)v=2000;
    auto write=[&](){return snap.writeRingExact((const uint8_t*)pcm,sizeof(pcm));};
    if(!strcmp(argv[1],"full-suspended-ring")) {
        snap._producerAwaitingResync=true;
        assert(!write());assert(snap.hasIncomingAudioSince(900));
        assert(snap._lastAudibleMs.load()==0); // Ingress is not proof of rendering.
        snap._serverMuted=true;assert(!snap.hasIncomingAudioSince(900));
        snap._serverMuted=false;testNow=2500;assert(!snap.hasIncomingAudioSince(900));
        for(auto&v:pcm)v=0;assert(!write());assert(!snap.hasIncomingAudioSince(900));
    } else if(!strcmp(argv[1],"fresh-after-long-idle")) {
        testNow=0x80000200u;write();assert(snap.hasIncomingAudioSince(100));
        testNow=20;write();assert(snap.hasIncomingAudioSince(0xfffffff0u));
        testNow=50;assert(!snap.hasIncomingAudioSince(30));
    } else return 2;
    puts("PASS");
}
