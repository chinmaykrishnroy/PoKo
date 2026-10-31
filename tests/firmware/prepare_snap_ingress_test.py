"""Exercise actual decoded-PCM ingress even when a suspended ring is full."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=(root/"SnapPlayer.cpp").read_text(encoding="utf-8")
a=s[s.index("bool SnapPlayer::writeRingExact("):s.index("bool SnapPlayer::ensureEncodedScratch(")]
b=s[s.index("bool SnapPlayer::hasIncomingAudioSince("):s.index("bool SnapPlayer::isSuspended()")]
(root/"build/snap-ingress-test.cpp").write_text('#include "snap_ingress_fixture.h"\n'+a+b+'\n#include "snap_ingress_main.cpp"\n',encoding="utf-8")
