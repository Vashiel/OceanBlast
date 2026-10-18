#include "core/machine.h"
#include "cpu/x64_execution_engine.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace oceanblast;
static int failures=0;
static void check(const char* name,bool ok) {
    std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n'; failures+=!ok;
}
static bool equivalent(const Machine& left,const Machine& right) {
    if (left.state().cpsr!=right.state().cpsr || left.execution().time().ticks!=right.execution().time().ticks) return false;
    for (unsigned reg=0;reg<16;++reg) if (left.state().r[reg]!=right.state().r[reg]) return false;
    return true;
}
int main() {
    {
        Machine machine,reference;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        bool same=true; size_t cases=0;
        for(unsigned condition=0;condition<15;++condition)
        for(unsigned flags=0;flags<16;++flags)
        for(uint32_t branch:{0x0a000003u,0x0bfffffcu}) {
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0x30008000; target->state().r[14]=0x12345678;
                target->state().cpsr=(flags<<28)|0xd3;
                target->bus().write32(0x30008000,(condition<<28)|branch);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equivalent(machine,reference); ++cases;
        }
        check("ARM conditional forward B and backward BL preserve flags target link and phase",
            same && native.stats().generatedSteps==cases && native.stats().fallbackSteps==0);
        same=true; cases=0;
        const auto before=native.stats().generatedSteps;
        for(unsigned condition=0;condition<15;++condition)
        for(unsigned flags=0;flags<16;++flags)
        for(bool backward:{false,true}) {
            const uint16_t instruction=condition==14?uint16_t(backward?0xe7fcu:0xe003u):
                uint16_t(0xd000u|(condition<<8)|(backward?0xfcu:3u));
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0x30008000; target->state().r[14]=0x12345678;
                target->state().cpsr=(flags<<28)|0xd3|FLAG_T;
                target->bus().write16(0x30008000,instruction);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equivalent(machine,reference); ++cases;
        }
        check("Thumb conditional and unconditional forward backward branches preserve scalar state",
            same && native.stats().generatedSteps-before==cases && native.stats().fallbackSteps==0);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xe3a00001); machine.bus().write32(4,0xe3a00002); machine.bus().write32(8,0xe3a00003);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),{2,65536});
        machine.execution().selectEngine(native);
        for(uint32_t pc : {0u,4u,0u,8u,4u}) { machine.state().r[15]=pc; machine.execution().run(1); }
        check("Indexed bounded cache evicts the least recently used block and recompiles safely",
            machine.cpu().getReg(0)==2 && native.stats().cacheHits==1 && native.stats().compilations==4 && native.stats().cacheEvictions==2);
    }
    for (size_t ratio : {1u,3u,6u,16u}) {
        Machine machine,reference;
        for (Machine* target : {&machine,&reference}) {
            for (unsigned offset=0;offset<256;offset+=4) target->bus().write32(offset,0xe2800001);
            target->bus().write32(256,0xeaffffbe); // B 0
            target->configureExecution(false,ratio);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        size_t steps=0;
        while (steps<97) {
            const size_t count=machine.execution().run(97-steps);
            if (!count) break;
            reference.execution().run(count); steps+=count;
        }
        check("Native batches and direct branches preserve state and legacy phase",
            steps==97 && equivalent(machine,reference) && native.stats().generatedSteps==97 &&
            native.stats().fallbackSteps==0 && native.stats().compilationFailures==0);
        machine.execution().selectInterpreter(machine.cpu());
        machine.execution().run(7); reference.execution().run(7);
        check("Switching from native execution to interpreter preserves the next steps",equivalent(machine,reference));
    }
    {
        Machine machine,reference;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        bool same=true;
        for (uint32_t raw : {0xe0002001u,0xe0202001u,0xe0402001u,0xe0602001u,0xe0802001u,
                             0xe1802001u,0xe1a02001u,0xe1c02001u,0xe1e02001u,0xe2822401u}) {
            for (uint32_t value : {0u,1u,0x7fffffffu,0x80000000u,0xffffffffu}) {
                for (Machine* target : {&machine,&reference}) {
                    target->state().r[0]=value; target->state().r[1]=0x80000001;
                    target->state().r[2]=value; target->state().r[15]=0;
                    target->state().cpsr=0x600000d3; target->bus().write32(0,raw);
                }
                machine.execution().run(1); reference.execution().run(1);
                same=same&&equivalent(machine,reference);
            }
        }
        check("All initial native integer lowerings preserve guest NZCV",same && native.stats().generatedSteps==50 && native.stats().compilationFailures==0);
        machine.bus().write32(0,0xe3a0002a); machine.state().r[15]=0;
        machine.execution().run(1);
        check("Cache validation observes code modified between batches",machine.state().r[0]==42 && native.stats().cacheHits>0);
    }
    {
        Machine machine,reference;
        for (Machine* target : {&machine,&reference}) {
            for (unsigned offset=0;offset<256;offset+=4) target->bus().write32(offset,0xe2800001);
            target->configureExecution(false,3);
            target->bus().write32(0x5100003c,2);
            target->bus().write32(0x51000008,(1u<<20)|(1u<<21));
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(1000,2);
        reference.execution().run(exit.steps);
        check("Native batch respects an external tick deadline",
            exit.reason==ExecutionExitReason::Deadline && exit.ticks==2 && exit.steps==4 && equivalent(machine,reference));
        const auto next=machine.execution().runToBoundary(1000);
        reference.execution().run(next.steps);
        check("Native batch respects the next modeled timer event",
            next.reason==ExecutionExitReason::Deadline && equivalent(machine,reference) &&
            machine.bus().getTimer4Expirations()==reference.bus().getTimer4Expirations());
    }
    {
        Machine machine,reference;
        for (Machine* target : {&machine,&reference}) {
            target->bus().write32(0,0xe2900001); // ADDS, native flags
            target->bus().write32(4,0xe12fff13); // BX r3, scalar fallback
            target->bus().write16(0x100,0x2007); // Thumb MOVS, native
            target->cpu().setReg(3,0x101);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        for (unsigned step=0;step<3;++step) { machine.execution().run(1); reference.execution().run(1); }
        check("Native flags and Thumb execution interoperate with scalar control-flow fallback",
            equivalent(machine,reference) && machine.cpu().isThumb() && machine.cpu().getReg(0)==7 &&
            native.stats().generatedSteps==2 && native.stats().fallbackSteps==1);
    }
    {
        Machine machine,reference;
        const uint32_t program[]={0xe2900001,0x02811001,0x12822001,0xe3b03000,0xe0b44005,0xe0d66007,0xe1b08069};
        for (Machine* target : {&machine,&reference}) {
            for (unsigned index=0;index<7;++index) target->bus().write32(index*4,program[index]);
            target->state().r[0]=0xffffffff; target->state().r[4]=0x7fffffff; target->state().r[9]=1;
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        machine.execution().run(7); reference.execution().run(7);
        check("One native block preserves flag dependencies conditional skips ADC SBC and RRX",
            equivalent(machine,reference) && native.stats().generatedSteps==7 && native.stats().compilations==1);
    }
    {
        Machine machine;
        machine.bus().write32(0x30004000,0x30000c02);
        machine.bus().write32(0x30004004,0x30000c02); // Virtual alias of code.
        machine.bus().write32(0x30000000,0xe3a00001);
        machine.state().cp15_control|=1; machine.state().cp15_ttb=0x30004000;
        machine.state().cp15_dacr=1; machine.state().cpsr=0x10;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        machine.execution().run(1);
        machine.bus().write32(0x00100000,0xe3a0002a);
        machine.state().r[15]=0; machine.execution().run(1);
        check("A physical code write through a virtual alias cannot reuse stale native code",
            machine.state().r[0]==42 && native.stats().compilations==2 && native.stats().generatedSteps==2);
        machine.bus().write32PhysSlow(0x30004000,0x30000402); // Privileged access only.
        machine.state().r[15]=0;
        machine.bus().flushTlb();
        const auto exit=machine.execution().runToBoundary(1);
        check("Changed MMU permissions are checked before cache reuse",
            exit.event==CpuStepEvent::PrefetchAbort && machine.cpu().getPC()==12 &&
            native.stats().generatedSteps==2 && native.stats().fallbackSteps==1);
        const auto ticks=machine.execution().time().ticks;
        const auto pc=machine.cpu().getPC();
        native.resetCodeCache();
        check("Code cache reset leaves architectural state and guest time intact",
            machine.cpu().getPC()==pc && machine.execution().time().ticks==ticks && native.stats().cacheResets==1);
        machine.cpu().reset(); machine.bus().write32(0,0xe3a00007);
        machine.execution().selectEngine(native); machine.execution().run(1);
        check("Execution safely recompiles after releasing the code cache",
            machine.cpu().getReg(0)==7 && native.stats().compilations==3);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xe2800001);
        machine.state().cpsr&=~FLAG_I;
        machine.bus().write32(0x4a000008,~(1u<<14));
        machine.bus().write32(0x5100003c,1); machine.bus().write32(0x51000008,(1u<<20)|(1u<<21));
        machine.bus().tick(machine.bus().ticksUntilEvent());
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(1);
        check("Eligible pending IRQ enters its vector before any generated guest operation",
            exit.event==CpuStepEvent::Interrupt && machine.cpu().getPC()==24 && machine.cpu().getReg(0)==0 &&
            native.stats().generatedSteps==0 && native.stats().fallbackSteps==1);
    }
    {
        const auto image=std::filesystem::temp_directory_path()/
            ("oceanblast-jit-level-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".img");
        if (std::filesystem::exists(image)) return 1;
        { std::ofstream file(image,std::ios::binary); const char zeros[4096]{}; file.write(zeros,sizeof(zeros)); }
        {
            Machine machine,reference;
            for (Machine* target : {&machine,&reference}) {
                for (unsigned offset=0;offset<128;offset+=4) target->bus().write32(offset,0xe2800001);
                target->configureExecution(false,3); target->execution().run(1);
                check("Synthetic USB image attaches for interrupt fixture",target->bus().attachUsbStorage(image.string()));
                target->bus().write32(0x49000010,0x80000040); // Master + root-hub interrupt.
                target->bus().write32(0x49000054,16); // Port reset asserts root-hub status change.
                target->bus().write32(0x4a000008,~(1u<<26));
                target->bus().write32(0x4a000000,1u<<26); target->bus().write32(0x4a000010,1u<<26);
                target->state().cpsr&=~FLAG_I;
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
            machine.execution().selectEngine(native);
            const bool needsRefresh=!machine.bus().hasPendingIrq() && machine.bus().needsLevelInterruptRefresh();
            const size_t first=machine.execution().runToBoundary(32).steps; reference.execution().run(first);
            const bool refreshed=needsRefresh && first==1 && equivalent(machine,reference) && machine.bus().hasPendingIrq();
            const auto exit=machine.execution().runToBoundary(32); reference.execution().run(exit.steps);
            check("Zero-tick USB level reassertion interrupts before the next guest instruction",
                refreshed && exit.event==CpuStepEvent::Interrupt && equivalent(machine,reference) && machine.cpu().getReg(0)==2);
        }
        std::filesystem::remove(image);
    }
    {
        Machine machine,reference;
        for (Machine* target : {&machine,&reference}) {
            target->bus().write32(0,0xe2800001); target->configureExecution(true,1);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.execution().selectEngine(native);
        machine.execution().run(1); reference.execution().run(1);
        check("Experimental register clocks retain scalar accounting",
            equivalent(machine,reference) && native.stats().fallbackSteps==1 && native.stats().generatedSteps==0);
    }
    std::cout<<failures<<" failure(s)\n";
    return failures?1:0;
}
