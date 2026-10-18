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
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,6);
            const uint32_t code[]={0xe3a00000,0xe3a01064,0xe2800001,0xe2511001,0x1afffffc};
            for(unsigned i=0;i<5;++i) target->bus().write32(i*4,code[i]);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(1024); reference.execution().run(302);
        check("A native suffix loop executes its setup once and repeats only the backward-edge body",
            exit.steps==302&&equivalent(machine,reference)&&machine.state().r[0]==100&&native.stats().internalBackedges==99);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u}) for(size_t phase=0;phase<ratio;++phase)
        for(uint32_t opcode:{0xe121f000u,0xee020f10u,0xef000000u,0xe12fff13u,0xe10f0000u}) for(bool bounded:{false,true}) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[0]=0x13; target->state().r[3]=0x101;
                target->bus().write32(0,opcode);
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto deadline=bounded?machine.execution().time().ticks+1:UINT64_MAX;
            const auto actual=machine.execution().runToBoundary(1,deadline),expected=reference.execution().runToBoundary(1,deadline);
            same=same&&equivalent(machine,reference)&&actual.reason==expected.reason&&actual.steps==expected.steps&&
                actual.ticks==expected.ticks&&actual.event==expected.event&&machine.state().cp15_ttb==reference.state().cp15_ttb;
        }
        check("Checked scalar exits retain exact context, exception and deadline reasons at every tested phase",same);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,6); target->state().r[0]=5000;
            target->bus().write32(0,0xe2500001); target->bus().write32(4,0x1afffffd);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(4096); reference.execution().run(4096);
        check("A validated native loop can retire beyond the source limit in one precise execution window",
            exit.steps==4096&&equivalent(machine,reference)&&native.stats().internalBackedges==2047);
    }
    {
        bool same=true;
        for(uint32_t flags=0;flags<16;++flags) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->state().cpsr=0xd3|(flags<<28); target->state().r[1]=17; target->state().r[3]=9;
                const uint32_t code[]={0x02810001,0xe2812003,0x12833001,0xe2834002,0xe12fff1e};
                for(unsigned i=0;i<5;++i) target->bus().write32(i*4,code[i]);
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            machine.execution().run(4); reference.execution().run(4);
            same=same&&equivalent(machine,reference);
        }
        check("Register caching preserves first-use operands and destinations across skipped predicates",same);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0;
                target->bus().write32(0,0xe2800001); target->bus().write32(4,0xea00000d);
                target->bus().write32(0x40,0xe2811001); target->bus().write32(0x44,0xeaffffed);
            }
            NativeCacheLimits limits; limits.followDirectBranches=true; limits.compileThreshold=3;
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),limits); machine.execution().selectEngine(native);
            machine.execution().run(193); reference.execution().run(193);
            const auto deadline=machine.execution().time().ticks+5;
            const auto actual=machine.execution().run(32,deadline),expected=reference.execution().run(32,deadline);
            same=same&&actual==expected&&equivalent(machine,reference);
            for(Machine* target:{&machine,&reference}) {
                target->bus().write32(0x40,0xe2811007); target->state().r[15]=0;
            }
            machine.execution().run(37); reference.execution().run(37);
            same=same&&equivalent(machine,reference);
        }
        check("Direct block links preserve promotion, all legacy phases, deadlines and noncontiguous code invalidation",same);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x3f8,0xe2800001); target->bus().write32(0x3fc,0xeaffff07);
            target->bus().write32(0x20,0xe2811001); target->bus().write32(0x24,0xea0000f3);
            target->state().r[15]=0x3f8;
        }
        NativeCacheLimits limits; limits.followDirectBranches=true;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),limits); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32); reference.execution().run(32);
        machine.state().r[15]=reference.state().r[15]=0x3f8;
        machine.bus().flushTlb(); reference.bus().flushTlb();
        machine.execution().run(32); reference.execution().run(32);
        check("A direct trace may link backward within its permission page and revalidate its mapping",
            exit.steps==32&&equivalent(machine,reference)&&native.stats().mappingRevalidations>0);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0,0xe2500001); target->bus().write32(4,0x1afffffd); target->state().r[0]=7;
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32); reference.execution().run(14);
        check("An internal backedge repeats full guest work and exits at the not-taken branch",
            exit.steps==14 && equivalent(machine,reference) && machine.cpu().getPC()==8 && native.stats().internalBackedges==6);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[0]=1000;
                target->bus().write32(0,0xe2500001); target->bus().write32(4,0x1afffffd);
            }
            const auto deadline=machine.execution().time().ticks+2;
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto steps=machine.execution().run(32,deadline); const auto expected=reference.execution().run(32,deadline);
            same=same&&steps==expected && equivalent(machine,reference) && machine.execution().time().ticks==deadline;
        }
        check("Internal loops preserve every legacy phase and stop at external deadlines",same);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) target->bus().write32(0,0xe2800001);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(uint32_t masks:{FLAG_I|FLAG_F,FLAG_F,0u}) {
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0; target->state().cpsr=(target->state().cpsr&~(FLAG_I|FLAG_F))|masks;
            }
            machine.execution().run(1); reference.execution().run(1);
        }
        const bool shared=equivalent(machine,reference) && native.stats().compilations==1;
        for(Machine* target:{&machine,&reference}) {
            target->state().r[15]=0; target->bus().write32(0x4a000008,~(1u<<14));
            target->bus().write32(0x5100003c,1); target->bus().write32(0x51000008,(1u<<20)|(1u<<21));
            target->bus().tick(target->bus().ticksUntilEvent());
        }
        const auto exit=machine.execution().runToBoundary(1); reference.execution().run(1);
        check("Translations share IRQ-mask variants while live unmasked IRQ entry still precedes guest work",
            shared && equivalent(machine,reference) && exit.event==CpuStepEvent::Interrupt && machine.cpu().getReg(0)==3);
    }
    {
        size_t functionBytes=0;
        {
            Machine sample; sample.bus().write32(0,0xe3a00001);
            X64ExecutionEngine native(sample.state(),sample.cpu(),sample.bus()); sample.execution().selectEngine(native);
            sample.execution().run(1); functionBytes=native.stats().peakCacheBytes;
        }
        Machine machine; machine.bus().write32(0,0xe3a00001); machine.bus().write32(4,0xe3a00002);
        NativeCacheLimits limits; limits.blocks=3; limits.bytes=functionBytes; limits.compileThreshold=2;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),limits); machine.execution().selectEngine(native);
        for(uint32_t pc:{0u,0u,4u,4u,0u,0u}) { machine.state().r[15]=pc; machine.execution().run(1); }
        check("Promotion under a one-function code budget evicts safely without releasing the selected entry",
            functionBytes>0 && machine.cpu().getReg(0)==1 && native.stats().promotions==3 &&
            native.stats().cacheEvictions==2 && native.stats().peakCacheBytes<=limits.bytes);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,3);
            target->bus().write32(0,0xe2800001); target->bus().write32(4,0xeafffffd);
        }
        NativeCacheLimits limits; limits.compileThreshold=3;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),limits); machine.execution().selectEngine(native);
        machine.execution().run(4); reference.execution().run(4);
        const bool cold=equivalent(machine,reference) && native.stats().compilations==0 && native.stats().coldBlockSteps==4;
        machine.execution().run(6); reference.execution().run(6);
        check("Cold blocks use the reference timeline and promote only after repeated entries",
            cold && equivalent(machine,reference) && native.stats().compilations==1 && native.stats().promotions==1 &&
            native.stats().generatedSteps==6 && native.stats().fallbackSteps==4);
        machine.bus().write32(0,0xe2800002); reference.bus().write32(0,0xe2800002);
        machine.execution().run(6); reference.execution().run(6);
        check("Cold-to-native promotion revalidates code changed after earlier executions",
            equivalent(machine,reference) && native.stats().promotions==2 && machine.cpu().getReg(0)==11);
    }
    for(size_t threshold:{1u,3u}) {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->state().r[1]=0x51000008; target->state().r[2]=(1u<<20)|(1u<<21);
            target->state().cpsr&=~FLAG_I; target->bus().write32(0x4a000008,~(1u<<14));
            target->bus().write32(0x5100003c,2); target->bus().write32(0,0xe5812000);
            for(unsigned i=1;i<32;++i) target->bus().write32(i*4,0xe2800001);
        }
        NativeCacheLimits limits; limits.compileThreshold=threshold;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus(),limits); machine.execution().selectEngine(native);
        machine.execution().run(16); reference.execution().run(16);
        check("MMIO arming a timer discards the cached event window before subsequent generated work",
            equivalent(machine,reference) && machine.cpu().getLR()==reference.cpu().getLR() &&
            machine.bus().getMmio(0x4a000000)==reference.bus().getMmio(0x4a000000));
    }
    {
        Machine machine;
        machine.bus().write32(0,0xe3a0000b); machine.bus().write32(0x800,0xe3a00016);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        bool same=true;
        for(uint32_t pc:{0u,0x800u,0u,0u}) {
            machine.state().r[15]=pc; machine.execution().run(1);
            same=same&&machine.cpu().getReg(0)==(pc?22u:11u);
        }
        const bool hit=native.stats().directCacheHits==1;
        machine.bus().write32(0,0xe3a0002a); machine.state().r[15]=0; machine.execution().run(1);
        check("Direct cache collisions check full identity and code writes invalidate hot reuse",
            same && hit && machine.cpu().getReg(0)==42 && native.stats().compilations==3);
    }
    {
        Machine machine;
        for(unsigned i=0;i<4;++i) machine.bus().write32(i*4,0xe2800001);
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(4);
        for(unsigned i=0;i<2;++i) { machine.state().r[15]=0; machine.execution().run(1); }
        machine.state().r[15]=0;
        const auto exit=machine.execution().runToBoundary(4);
        check("A hot short translation cannot hide a cached longer block on a larger budget",
            exit.steps==4 && machine.cpu().getReg(0)==10 && native.stats().compilations==2);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,3); target->bus().write32(0,0xe12fff13); target->state().r[3]=0;
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(7); reference.execution().run(7);
        check("Repeated unsupported instructions reuse a checked scalar exit with exact reference timing",
            equivalent(machine,reference) && native.stats().fallbackSteps==7 && native.stats().fallbackEntries==1 &&
            native.stats().fallbackCacheHits==6 && native.stats().compilations==0);
        for(Machine* target:{&machine,&reference}) { target->bus().write32(0,0xe3a0002a); target->state().r[15]=0; }
        machine.execution().run(1); reference.execution().run(1);
        check("A code write replaces a cached scalar exit with native execution",
            equivalent(machine,reference) && machine.cpu().getReg(0)==42 && native.stats().generatedSteps==1);
        native.resetCodeCache();
        for(Machine* target:{&machine,&reference}) { target->bus().write32(0,0xe12fff13); target->state().r[15]=0; }
        machine.execution().run(2); reference.execution().run(2);
        check("Cache reset releases native and scalar entries and safely recreates the scalar exit",
            equivalent(machine,reference) && native.stats().fallbackSteps==9 && native.stats().fallbackEntries==2 &&
            native.stats().fallbackCacheHits==7);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02); target->bus().write32(0x30004004,0x30000c02);
            target->bus().write32(0x30000000,0xe12fff13); target->state().r[3]=0;
            target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000; target->state().cp15_dacr=1;
            target->state().cpsr=0x10; target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(2); reference.execution().run(2);
        for(Machine* target:{&machine,&reference}) { target->bus().write32(0x00100000,0xe3a0002a); target->state().r[15]=0; }
        machine.execution().run(1); reference.execution().run(1);
        const bool alias=equivalent(machine,reference) && machine.cpu().getReg(0)==42;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32PhysSlow(0x30000000,0xe12fff13); target->state().r[15]=0;
        }
        machine.execution().run(1); reference.execution().run(1);
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32PhysSlow(0x30004000,0x30000402); target->bus().flushTlb(); target->state().r[15]=0;
        }
        const auto exit=machine.execution().runToBoundary(1); reference.execution().run(1);
        check("Cached scalar exits obey alias writes and changed instruction permissions",
            alias && equivalent(machine,reference) && exit.event==CpuStepEvent::PrefetchAbort && machine.cpu().getPC()==12);
    }
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
