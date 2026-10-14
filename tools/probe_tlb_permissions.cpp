#include "bus.h"
#include <iostream>
using namespace oceanblast;
int main() {
    constexpr u32 va = 0x40000000, ttb = 0x30100000;
    {
        Bus b; Bus::MmuFault fault;
        b.write32PhysSlow(ttb + 0x1000, 0x30000c02);
        b.write32PhysSlow(ttb + 0x1020, 0x30200402); // Same TLB index, AP=1.
        b.setTtb(ttb); b.setMmuEnabled(true); b.setUserMode(true);
        bool retained = b.translate(va, &fault) == 0x30000000 && fault == Bus::MmuFault::NONE;
        b.translate(va + 0x800000, &fault);
        retained = retained && fault == Bus::MmuFault::SECTION_PERMISSION_FAULT;
        b.write32PhysSlow(ttb + 0x1000, 0x30000402); // No invalidation of the cached AP=3 entry.
        retained = retained && b.translate(va, &fault) == 0x30000000 && fault == Bus::MmuFault::NONE;
        std::cout << "cold-permission-fault-preserves-colliding-entry=" << retained << '\n';
    }
    {
        Bus b; Bus::MmuFault fault;
        constexpr u32 largeVa = 0x41000000, pageTable = 0x3010c000, largePa = 0x30400000;
        b.write32PhysSlow(ttb + 0x1040, pageTable | 1);
        for (unsigned i = 0; i < 16; ++i)
            b.write32PhysSlow(pageTable + i * 4, largePa | 1 | (3 << 4) | (2 << 6) | (1 << 8));
        b.setTtb(ttb); b.setMmuEnabled(true); b.setUserMode(true);
        bool correct = true;
        for (bool user : {true, false, true}) {
            b.setUserMode(user);
            for (unsigned slice = 0; slice < 16; ++slice) for (bool write : {false, true}) {
                const unsigned ap = 3 - slice / 4;
                const bool allowed = !user || ap == 3 || (ap == 2 && !write);
                const u32 offset = slice * 0x1000 + 0x3c0;
                const u32 result = b.translate(largeVa + offset, &fault, write);
                correct = correct && (allowed
                    ? result == largePa + offset && fault == Bus::MmuFault::NONE
                    : result == UINT32_MAX && fault == Bus::MmuFault::PAGE_PERMISSION_FAULT);
            }
        }
        std::cout << "large-page-slices-and-mode-permissions=" << correct << '\n';
    }
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
