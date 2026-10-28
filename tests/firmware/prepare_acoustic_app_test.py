"""Production app/screens/DSP, replacing audio hardware and display transport."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=(root/"AcousticApp.cpp").read_text(encoding="utf-8")
source=source.replace('#include "AudioManager.h"','#include "acoustic_app_fixture.h"')
source=source.replace('#include "MicrophoneInput.h"','')
(root/"build/acoustic-app-test.cpp").write_text(source+'\n#include "acoustic_app_main.cpp"\n',encoding="utf-8")
