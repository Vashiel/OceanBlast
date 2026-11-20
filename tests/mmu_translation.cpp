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
    { Arm920TMmu edges;
      write(table,0);write(table+0x3ffc,0);
      edges.setTtb(table);edges.setEnabled(true);
      edges.translate(0x42,ram.data(),&fault);
      bool invalid=fault==Fault::SECTION_TRANSLATION_FAULT;
      edges.translate(0xffffffff,ram.data(),&fault);
      invalid=invalid&&fault==Fault::SECTION_TRANSLATION_FAULT;
      check("Invalid entries cannot match the lowest or highest virtual page",invalid);
      write(table,0x30000c02);write(table+0x3ffc,0x30000c02);
      edges.setUserMode(true);
      bool valid=edges.translate(0x42,ram.data(),&fault)==0x30000042&&fault==Fault::NONE;
      valid=valid&&edges.translate(0xfffff07a,ram.data(),&fault)==0x300ff07a&&fault==Fault::NONE;
      write(table+0x3ffc,0x30400c02);
      valid=valid&&edges.translate(0xffffffff,ram.data(),&fault)==0x300fffff&&fault==Fault::NONE;
      edges.flushTlbEntry(0xfffff000);
      valid=valid&&edges.translate(0xffffffff,ram.data(),&fault)==0x304fffff&&fault==Fault::NONE;
      check("Highest virtual page retains its mapping until single-entry invalidation",valid);
      write(table+0x3ffc,0x30000402);edges.flushTlb();
      edges.translate(0xffffffff,ram.data(),&fault);
      check("Full invalidation restores section denial at the highest virtual page",fault==Fault::SECTION_PERMISSION_FAULT);
    }
    { Arm920TMmu denied;
      write(table+0x1000,0x30108001);write(0x30108000,0x30000552);
      denied.setTtb(table);denied.setEnabled(true);
      bool consistent=denied.translate(va+0x234,ram.data(),&fault)==0x30000234;
      write(0x30108000,0x30000ff2);denied.setUserMode(true);
      denied.translate(va+0x234,ram.data(),&fault);
      consistent=consistent&&fault==Fault::PAGE_PERMISSION_FAULT&&
        denied.translate(va+0x234,ram.data(),nullptr,true)==UINT32_MAX;
      denied.flushTlb();
      consistent=consistent&&denied.translate(va+0x234,ram.data(),&fault)==0x30000234&&fault==Fault::NONE;
      check("Cached denial survives descriptor edits and null fault reporting until invalidation",consistent);
    }
    { Arm920TMmu large;bool equivalent=true;
      write(table+0x1000,0x30108001);large.setTtb(table);large.setEnabled(true);
      for(u32 rights=0;rights<256;++rights) {
        for(u32 slot=0;slot<16;++slot)write(0x30108000+slot*4,0x30020001|(rights<<4));
        large.flushTlb();large.setUserMode(false);
        for(u32 part=0;part<4;++part)large.translate(va+part*0x4000+0x234,ram.data(),&fault);
        for(bool user:{true,false,true}) {
          large.setUserMode(user);
          for(u32 part=0;part<4;++part)for(bool store:{false,true}) {
            const u32 ap=(rights>>(part*2))&3;
            const bool allowed=!user||ap==3||(ap==2&&!store);
            const auto result=large.translate(va+part*0x4000+0x234,ram.data(),&fault,store);
            equivalent=equivalent&&(allowed
              ? result==0x30020234+part*0x4000&&fault==Fault::NONE
              : result==UINT32_MAX&&fault==Fault::PAGE_PERMISSION_FAULT);
          }
        }
      }
      check("Cached large-page rights preserve every AP field and privilege transition",equivalent);
    }
    { Arm920TMmu fallback;fallback.setEnabled(true);bool allowed=true;
      for(bool user:{false,true,false,true}) {
        fallback.setUserMode(user);
        for(bool store:{false,true})allowed=allowed&&
          fallback.translate(0xc0001034,ram.data(),&fault,store)==0x30001034&&fault==Fault::NONE;
      }
      check("Synthetic kernel fallback retains read and write policy across cached mode changes",allowed);
    }
    { // LDRT/STRT: user permissions under the DACR domain policy, cold and cached.
      Arm920TMmu unprivileged; unprivileged.setTtb(table); unprivileged.setEnabled(true); unprivileged.setUserMode(false);
      bool policy=true;
      for (bool page : {false,true}) for (u32 ap=0;ap<4;++ap) {
        const u32 domain=page?2:5;
        if (page) { write(table+0x1000,0x30108001|(domain<<5)); write(0x30108000,ADDR_SDRAM_BASE|((ap*0x55u)<<4)|2); }
        else write(table+0x1000,0x30000002|(ap<<10)|(domain<<5));
        unprivileged.flushTlb();
        for (int pass=0;pass<2;++pass) for (u32 access=0;access<4;++access) for (bool store:{false,true}) {
          unprivileged.setDacr(access<<(domain*2));
          u32 reported=99;
          const u32 result=unprivileged.translateUnprivileged(va+0x10,ram.data(),&fault,store,&reported);
          const bool userAllowed=ap==3||(ap==2&&!store);
          const Fault expected = access==3 || (access==1 && userAllowed) ? Fault::NONE
              : access==1 ? (page?Fault::PAGE_PERMISSION_FAULT:Fault::SECTION_PERMISSION_FAULT)
              : (page?Fault::PAGE_DOMAIN_FAULT:Fault::SECTION_DOMAIN_FAULT);
          policy=policy&&fault==expected&&reported==domain&&
            (expected==Fault::NONE ? result==ADDR_SDRAM_BASE+0x10 : result==UINT32_MAX);
        }
      }
      check("Unprivileged translation applies domain access and user AP for sections and pages",policy);
      write(table+0x1000,0x30000802|(1u<<5)); unprivileged.flushTlb(); unprivileged.setDacr(0xcu);
      u32 reported=0;
      bool cached=unprivileged.translateUnprivileged(va,ram.data(),&fault,true,&reported)==0x30000000&&fault==Fault::NONE;
      unprivileged.setDacr(0x4u);
      cached=cached&&unprivileged.translateUnprivileged(va,ram.data(),&fault,true,&reported)==UINT32_MAX&&
             fault==Fault::SECTION_PERMISSION_FAULT;
      check("A DACR change applies to an already cached translation",cached);
      check("Privileged ordinary translation keeps ignoring user AP",
            unprivileged.translate(va,ram.data(),&fault,true)==0x30000000&&fault==Fault::NONE);
      unprivileged.setDacr(0);
      check("Emulator kernel fallback mapping stays outside the domain check",
            unprivileged.translateUnprivileged(0xc0001034,ram.data(),&fault,true,&reported)==0x30001034&&fault==Fault::NONE);
    }
    std::cout << failures << " failure(s)\n";
    return failures?1:0;
}
