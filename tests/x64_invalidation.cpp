#include "core/machine.h"
#include "cpu/x64_execution_engine.h"
#include <cstring>
#include <iostream>
using namespace oceanblast;
static int failures=0;
static void check(const char* name,bool ok) { std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n'; failures+=!ok; }
int main() {
    {
        Machine machine; constexpr uint32_t pc=0x30008000;
        machine.bus().write32(pc,0xe3a00001); machine.state().r[15]=pc;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(1); machine.state().r[15]=pc; machine.execution().run(1);
        check("Unchanged code uses generation-validated cache entries",native.stats().fastCacheHits==1);
        bool same=true;
        machine.bus().write8(pc,42); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==42;
        machine.bus().write16(pc,7); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==7;
        machine.bus().write32(pc,0xe3a00009); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==9;
        machine.bus().write32PhysSlow(pc,0xe3a0000b); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==11;
        machine.bus().write8Phys(pc+0x02000000,13); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==13;
        machine.bus().write16PhysSlow(pc+0x02000000,15); machine.state().r[15]=pc; machine.execution().run(1); same&=machine.cpu().getReg(0)==15;
        check("Byte halfword word slow and mirrored writes invalidate affected code",same && native.stats().sourceValidations>=6);
        const uint32_t changed=0xe3a00063;
        std::memcpy(const_cast<uint8_t*>(machine.bus().getSdramPtr())+0x8000,&changed,4);
        machine.bus().invalidateCodeEpoch(); machine.state().r[15]=pc; machine.execution().run(1);
        check("Bulk DMA epoch invalidates code changed outside CPU stores",machine.cpu().getReg(0)==99);
    }
    {
        Machine machine;
        machine.bus().write32(0x300083fc,0xe3a00001); machine.bus().write32(0x30008400,0xe3a00002);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(uint32_t pc:{0x300083fcu,0x30008400u}) { machine.state().r[15]=pc; machine.execution().run(1); }
        machine.bus().write16(0x300083ff,0x2ae3);
        machine.state().r[15]=0x30008400; machine.execution().run(1);
        const bool second=machine.cpu().getReg(0)==42;
        machine.state().r[15]=0x300083fc; machine.execution().run(1);
        check("An unaligned write across code-page boundaries invalidates both dependencies",second && machine.cpu().getReg(0)==1);
    }
    {
        Machine machine; machine.bus().write32(0,0xe3a00001);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(1); machine.bus().reset(); machine.state().r[15]=0; machine.state().r[0]=77;
        machine.execution().run(1);
        check("Whole-memory reset cannot execute retained pre-reset code",machine.cpu().getReg(0)==77);
    }
    std::cout<<failures<<" failure(s)\n"; return failures?1:0;
}
