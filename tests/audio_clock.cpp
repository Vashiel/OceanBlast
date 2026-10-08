#include "memory/bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    int errors=0;
    auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';errors+=!ok;};
    Bus b; b.reset();
    check("Reset uses the 12-MHz crystal before MPLLCON is written",b.getCpuClock()==12000000 && b.getPeripheralClock()==12000000);
    b.write32(0x4c000004,0x52011);b.write32(0x4c000014,3);
    check("Programmed board clocks are 180/90/45 MHz",b.getCpuClock()==180000000 && b.getBusClock()==90000000 && b.getPeripheralClock()==45000000);
    b.write32(0x55000000,3);b.write32(0x55000008,0xe7);b.write32(0x55000004,0x99);
    check("IIS preserves the calculated 21972-Hz rate",b.getAudioSampleRate()==21972);
    b.write32(0x55000004,0x9d);
    check("IIS bit 2 selects 384fs",b.getAudioSampleRate()==45000000u/(8*384));
    b.write32(0x55000004,0x9b);
    check("IIS serial-clock bit 1 does not change the master-clock ratio",b.getAudioSampleRate()==21972);
    b.write32(0x55000004,0x99);b.write32(0x55000008,3u<<5);
    check("IIS prescaler A preserves the calculated 43945-Hz rate",b.getAudioSampleRate()==43945);
    b.write32(0x55000008,15u<<5);
    check("IIS prescaler A preserves the calculated 10986-Hz rate",b.getAudioSampleRate()==10986);
    b.write32(0x4c000014,0);check("Bus divider writes update the IIS clock",b.getAudioSampleRate()==43945);
    b.write32(0x4c000014,4);check("S3C2410A special 1:4:4 divider is supported",b.getBusClock()==45000000 && b.getPeripheralClock()==45000000);
    b.write32(0x4c000010,0x14);check("Slow mode derives from the crystal rather than PLL",b.getCpuClock()==1500000 && b.getPeripheralClock()==375000);
    b.write32(0x4c000010,0x10);check("Slow mode zero divider uses the input clock",b.getCpuClock()==12000000);
    b.write32(0x4c000010,4);b.write32(0x4c000014,3);b.write32(0x55000000,1);
    check("Disabling the IIS prescaler bypasses divider A",b.getAudioSampleRate()==175781);
    b.reset();check("Reset restores crystal clock selection",b.getCpuClock()==12000000);
    return errors?1:0;
}
