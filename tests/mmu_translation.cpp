#include "memory/arm920t_mmu.h"
#include <iostream>
#include <vector>
using namespace oceanblast;
static int failures = 0;
static void check(const char* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
int main() {
    std::vector<u8> ram(ADDR_SDRAM_SIZE);
    auto write = [&](u32 address,u32 value) {
        std::memcpy(ram.data()+address-ADDR_SDRAM_BASE,&value,sizeof(value));
    };
    Arm920TMmu mmu;
    using Fault = Arm920TMmu::MmuFault;
    Fault fault = Fault::NONE;
    constexpr u32 va=0x40000000, table=0x30100000;
    check("Disabled MMU preserves physical addresses",
        mmu.translate(0x12345678,ram.data(),&fault)==0x12345678 && fault==Fault::NONE);
    write(table+0x1000,0x30000c02);
    mmu.setTtb(table); mmu.setEnabled(true); mmu.setUserMode(true);
    check("Standalone MMU translates a user-accessible section",
        mmu.translate(va+0x234,ram.data(),&fault)==0x30000234 && fault==Fault::NONE);
    write(table+0x1000,0x30200c02);
    check("Cached mapping survives descriptor changes until invalidation",
        mmu.translate(va,ram.data(),&fault)==0x30000000);
    mmu.flushTlbEntry(va);
    check("Single-entry invalidation reveals the changed physical mapping",
        mmu.translate(va,ram.data(),&fault)==0x30200000);
    write(table+0x1020,0x30400402);
    mmu.translate(va+0x800000,ram.data(),&fault);
    const bool denied=fault==Fault::SECTION_PERMISSION_FAULT;
    write(table+0x1000,0x30000402);
    check("Denied cold translation preserves a colliding cached entry",
        denied && mmu.translate(va,ram.data(),&fault)==0x30200000 && fault==Fault::NONE);
    mmu.flushTlb(); mmu.translate(va,ram.data(),&fault);
    check("Full invalidation applies current section permissions",fault==Fault::SECTION_PERMISSION_FAULT);
    write(table+0x1000,0x30108001); write(0x30108000,0x30000ff2); mmu.flushTlb();
    bool pages=true;
    for (u32 offset : {0u,0x400u,0x800u,0xffcu})
        pages=pages && mmu.translate(va+offset,ram.data(),&fault)==ADDR_SDRAM_BASE+offset && fault==Fault::NONE;
    check("Standalone MMU translates all offsets of an accessible small page",pages);
    mmu.translate(0x60000000,ram.data(),&fault);
    check("Unmapped address returns a translation fault",fault==Fault::SECTION_TRANSLATION_FAULT);
    std::cout << failures << " failure(s)\n";
    return failures?1:0;
}
