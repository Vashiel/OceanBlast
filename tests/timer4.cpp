#include "memory/bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    int errors=0;
    auto check=[&](const char* text,bool ok){std::cout<<(ok ? "PASS " : "FAIL ")<<text<<'\n';errors+=!ok;};
    { Timer4 timer;timer.setClock(45000000);timer.configure(2u<<8,0);
      timer.setBuffer(37499);timer.control((1u<<22)|(1u<<21)|(1u<<20));
      timer.advance(350000);
      check("Expiration count includes every elapsed reload in a bulk advance",timer.getExpirations()==3 && timer.observe()==18749);
      timer.reset();check("Reset clears expiration observations",timer.getExpirations()==0); }
    { Bus diagnostic;diagnostic.reset();diagnostic.setMmioProfiling(true);
      diagnostic.write32(0x4c000004,0x52011);diagnostic.write32(0x4c000014,3);
      diagnostic.write32(0x51000000,2u<<8);diagnostic.write32(0x5100003c,37499);
      diagnostic.write32(0x51000008,(1u<<22)|(1u<<21)|(1u<<20));
      diagnostic.tick(100000);diagnostic.tick(100000);
      const auto& d=diagnostic.getIrqDiagnostics();
      check("Masked repeated timer requests preserve one pending bit and count coalescence",diagnostic.getTimer4Expirations()==2 && d.requests[14]==2 && d.alreadyPending[14]==1 && !diagnostic.hasPendingIrq());
      diagnostic.write32(0x4a000008,~(1u<<14));
      diagnostic.write32(0x4a000000,1u<<14);
      check("Source acknowledgement does not acknowledge the selected interrupt",d.sourceClears[14]==1 && d.selectedClears[14]==0 && diagnostic.hasPendingIrq());
      diagnostic.write32(0x4a000010,1u<<14);
      diagnostic.write32(0x4a000010,1u<<14);
      check("Selected acknowledgement counts only a previously pending bit",d.selectedClears[14]==1 && !diagnostic.hasPendingIrq());
      diagnostic.reset();check("Reset clears interrupt observations",d.requests[14]==0 && d.sourceClears[14]==0); }
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
