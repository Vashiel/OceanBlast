#include "memory/bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    int errors=0;
    auto check=[&](const char* text,bool ok){std::cout<<(ok ? "PASS " : "FAIL ")<<text<<'\n';errors+=!ok;};
    Bus b; b.reset();
    b.write32(0x4c000004,0x52011); b.write32(0x4c000014,3);
    constexpr u32 base=0x51000000, count=base+0x40;
    b.write32(base, 2u<<8); b.write32(base+4,0); b.write32(base+0x3c,37499);
    b.write32(base+8, (1u<<22)|(1u<<21));
    check("Timer manual update loads the count buffer",b.read32(count)==37499);
    check("Repeated timer reads do not advance guest time",b.read32(count)==37499 && b.read32(count)==37499);
    b.tick(1000);check("Stopped timer preserves observation",b.read32(count)==37499);
    b.write32(base+8,(1u<<22)|(1u<<20)); b.tick(99999);
    check("Timer reaches zero before its programmed period",b.read32(count)==0 && !(b.read32(0x4a000000)&(1u<<14)));
    b.tick(1);check("Timer reload and interrupt share a 100000-instruction boundary",b.read32(count)==37499 && (b.read32(0x4a000000)&(1u<<14)));
    b.write32(0x4a000000,1u<<14);b.write32(0x4a000010,1u<<14);
    b.tick(150000);check("Bulk ticking retains fractional period after reload",b.read32(count)==18749);
    b.write32(base+8,1u<<22);u32 held=b.read32(count);b.tick(200000);
    check("Clearing START freezes the timer",b.read32(count)==held);
    b.write32(base+0x3c,99);check("Count buffer write waits for manual update",b.read32(count)==held);
    b.write32(base+8,(1u<<21)|(1u<<20));b.tick(267);
    check("One-shot countdown stops at zero",b.read32(count)==0);
    b.write32(0x4a000000,1u<<14);b.tick(100000);
    check("One-shot does not repeatedly interrupt",!(b.read32(0x4a000000)&(1u<<14)));
    b.write32(base+4,4u<<16);b.write32(base+8,(1u<<21)|(1u<<20));b.tick(100000);
    check("Unmodeled external clock does not invent elapsed ticks",b.read32(count)==99);
    b.reset();check("Bus reset clears timer and interrupt state",b.read32(count)==0 && !b.hasPendingIrq() && b.read32(base+8)==0);
    b.write32(0x4c000004,0x52011);b.write32(0x4c000014,3);
    b.write32(base,2u<<8);b.write32(base+4,0);b.write32(base+0x3c,37499);
    b.write32(base+8,(1u<<22)|(1u<<21)|(1u<<20));b.tick(50000);
    b.write32(0x4c000004,0x52012);b.tick(99999);
    check("Changing PCLK preserves timer phase and changes elapsed rate",b.read32(count)==0 && !(b.read32(0x4a000000)&(1u<<14)));
    b.tick(1);check("Timer expires at the updated PCLK boundary",b.read32(count)==37499 && (b.read32(0x4a000000)&(1u<<14)));
    return errors?1:0;
}
