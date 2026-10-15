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
    // Prime one subregion, then visit every other subregion without flushing.
    // Cold and cached accesses must agree across read/write and privilege changes.
    bool permissions=true;
    for (u32 descriptorType : {2u,3u}) for (u32 rights=0;rights<256;++rights) {
        write(table+0x1000,0x30108001);
        write(0x30108000,ADDR_SDRAM_BASE|(rights<<4)|descriptorType);
        mmu.flushTlb(); mmu.setUserMode(false);
        mmu.translate(va,ram.data(),&fault);
        for (bool user : {true,false,true}) {
            mmu.setUserMode(user);
            for (u32 part=0;part<4;++part) for (bool store : {false,true}) {
                const u32 ap=(rights>>(part*2))&3;
                const bool allowed=!user || ap==3 || (ap==2 && !store);
                const auto address=va+part*0x400+0x3c;
                const auto result=mmu.translate(address,ram.data(),&fault,store);
                permissions=permissions && (allowed
                    ? result==ADDR_SDRAM_BASE+part*0x400+0x3c && fault==Fault::NONE
                    : result==UINT32_MAX && fault==Fault::PAGE_PERMISSION_FAULT);
            }
        }
    }
    check("Cached small-page permissions cover every AP combination and mode transition",permissions);
    write(table+0x1000,0x30000802); mmu.flushTlb(); mmu.setUserMode(false);
    mmu.translate(va,ram.data(),&fault); mmu.setUserMode(true);
    mmu.translate(va,ram.data(),&fault,true); const auto warmFault=fault;
    mmu.flushTlb(); mmu.translate(va,ram.data(),&fault,true);
    check("Cached and cold section permission faults have the same section type",
        warmFault==Fault::SECTION_PERMISSION_FAULT && fault==warmFault);
    std::cout << failures << " failure(s)\n";
    return failures?1:0;
}
