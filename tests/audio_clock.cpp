#include "memory/bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    int errors=0;
    auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';errors+=!ok;};
    Bus b; b.reset();b.write32(0x55000008,0xe7);b.write32(0x55000004,0x99);
    check("IIS 256fs preserves nominal 22050-Hz mode",b.getAudioSampleRate()==22050);
    b.write32(0x55000004,0x9d);
    check("IIS bit 2 selects 384fs",b.getAudioSampleRate()==45000000u/(8*384));
    b.write32(0x55000004,0x9b);
    check("IIS serial-clock bit 1 does not change the master-clock ratio",b.getAudioSampleRate()==22050);
    b.write32(0x55000004,0x99);b.write32(0x55000008,3u<<5);
    check("IIS prescaler A supports nominal 44100-Hz mode",b.getAudioSampleRate()==44100);
    b.write32(0x55000008,15u<<5);
    check("IIS prescaler A supports nominal 11025-Hz mode",b.getAudioSampleRate()==11025);
    return errors?1:0;
}
