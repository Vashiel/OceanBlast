#include "bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    constexpr u32 va = 0x40000000, ttb = 0x30100000;
    {
        Bus b; Bus::MmuFault fault;
        b.write32PhysSlow(ttb + 0x1000, 0x30000402); // Section, AP=1.
        b.setTtb(ttb); b.setMmuEnabled(true);
        b.translate(va, &fault); // Prime a privileged-only TLB entry.
        b.write32PhysSlow(ttb + 0x1000, 0x30000c02); // AP=3, without invalidation.
        b.setUserMode(true);
        const u32 cached = b.translate(va, &fault);
        std::cout << "cached-denial-preserved=" << (cached == UINT32_MAX && fault != Bus::MmuFault::NONE)
                  << " fault=" << unsigned(fault) << '\n';
        b.flushTlb();
        const u32 afterFlush = b.translate(va, &fault);
        std::cout << "changed-table-visible-after-flush=" << (afterFlush == 0x30000000 && fault == Bus::MmuFault::NONE) << '\n';
    }
    {
        Bus b; Bus::MmuFault fault;
        constexpr u32 pageTable = 0x30108000;
        b.write32PhysSlow(ttb + 0x1000, pageTable | 1); // Coarse page table.
        b.write32PhysSlow(pageTable, 0x30000000 | 2 | (3 << 4) | (1 << 6) | (3 << 8) | (3 << 10));
        b.setTtb(ttb); b.setMmuEnabled(true); b.setUserMode(true);
        const u32 allowed = b.translate(va, &fault);
        const bool firstAllowed = allowed == 0x30000000 && fault == Bus::MmuFault::NONE;
        const u32 restricted = b.translate(va + 0x400, &fault);
        std::cout << "subpage-denial-preserved=" << (firstAllowed && restricted == UINT32_MAX && fault == Bus::MmuFault::PAGE_PERMISSION_FAULT) << '\n';
        b.flushTlb();
        b.translate(va + 0x400, &fault);
        std::cout << "subpage-denied-with-cold-tlb=" << (fault == Bus::MmuFault::PAGE_PERMISSION_FAULT) << '\n';
    }
    {
        Bus b; Bus::MmuFault fault;
        b.write32PhysSlow(ttb + 0x1000, 0x30000802); // Section, AP=2.
        b.setTtb(ttb); b.setMmuEnabled(true);
        b.translate(va, &fault); b.setUserMode(true);
        b.translate(va, &fault, true);
        std::cout << "warm-section-write-fault=" << unsigned(fault) << '\n';
        b.flushTlb(); b.translate(va, &fault, true);
        std::cout << "cold-section-write-fault=" << unsigned(fault) << '\n';
    }
    {
        Bus b; Bus::MmuFault firstFault, secondFault;
        b.setMmuEnabled(true); b.setUserMode(true);
        const u32 first = b.translate(0xc0000000, &firstFault);
        const u32 second = b.translate(0xc0000000, &secondFault);
        std::cout << "kernel-fallback-cold-warm-consistent=" << (first == second && firstFault == secondFault)
                  << " first-fault=" << unsigned(firstFault) << " second-fault=" << unsigned(secondFault) << '\n';
    }
    {
        Bus b; Bus::MmuFault firstFault, secondFault;
        constexpr u32 pageTable = 0x30108000;
        b.write32PhysSlow(ttb + 0x1000, pageTable | 1);
        b.write32PhysSlow(pageTable, 0x30000000 | 3 | (3 << 4) | (1 << 6));
        b.setTtb(ttb); b.setMmuEnabled(true); b.setUserMode(true);
        const u32 first = b.translate(va + 0x400, &firstFault);
        const u32 second = b.translate(va + 0x400, &secondFault);
        std::cout << "type3-cold-warm-consistent=" << (first == second && firstFault == secondFault)
                  << " first-fault=" << unsigned(firstFault) << " second-fault=" << unsigned(secondFault) << '\n';
    }
}
