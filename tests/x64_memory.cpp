#include "core/machine.h"
#include "cpu/x64_execution_engine.h"
#include <iostream>
using namespace oceanblast;
static int failures=0;
static void check(const char* name,bool ok) { std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n'; failures+=!ok; }
static bool equal(const Machine& a,const Machine& b) {
    if (a.state().cpsr!=b.state().cpsr || a.state().spsr!=b.state().spsr || a.execution().time().ticks!=b.execution().time().ticks) return false;
    for (unsigned i=0;i<16;++i) if(a.state().r[i]!=b.state().r[i])return false;
    return a.state().cp15_far==b.state().cp15_far && a.state().cp15_fsr==b.state().cp15_fsr;
}
int main() {
    {
        Machine machine,reference; bool same=true; uint64_t cases=0;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(unsigned kind=0;kind<4;++kind) for(bool immediate:{false,true}) for(unsigned indexing=0;indexing<3;++indexing)
        for(bool up:{false,true}) for(unsigned alignment:{0u,1u}) for(uint32_t flags:{0u,FLAG_Z}) {
            const bool load=kind!=1,pre=indexing!=0,writeback=indexing==2;
            const unsigned operation=kind<2?1:kind;
            const uint32_t address=0x30001008+alignment;
            const uint32_t raw=(uint32_t(pre)<<24)|(uint32_t(up)<<23)|(uint32_t(immediate)<<22)|
                (uint32_t(writeback)<<21)|(uint32_t(load)<<20)|(1u<<16)|(2u<<12)|0x90u|(operation<<5)|(immediate?3u:4u);
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0; target->state().cpsr=0xd3|flags;
                target->state().r[1]=pre?(up?address-3:address+3):address;
                target->state().r[2]=0x1234fedc;target->state().r[4]=3;
                target->bus().write32(0,raw);
                for(unsigned offset=0;offset<24;++offset)target->bus().write8(0x30001000+offset,uint8_t(0x80+offset));
            }
            machine.execution().run(1);reference.execution().run(1);
            same=same&&equal(machine,reference);
            for(unsigned offset=0;offset<24;++offset)same=same&&machine.bus().read8(0x30001000+offset)==reference.bus().read8(0x30001000+offset);
            ++cases;
        }
        check("Native halfword and signed loads preserve indexing, offsets, odd addresses, predication and writeback",
            same&&cases==192&&native.stats().generatedSteps==cases&&native.stats().memorySteps==cases/2&&native.stats().fallbackSteps==0);
    }
    for(uint32_t opcode:{0xe1f120b4u,0xe1e120b4u,0xe1f120d4u,0xe1f120f4u}) {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02);
            target->bus().write32(0x30000000,opcode);
            target->state().cp15_control|=1;target->state().cp15_ttb=0x30004000;target->state().cp15_dacr=1;
            target->state().cpsr=0x10;target->state().r[1]=0x200000;target->state().r[2]=0xfedcba98;
            target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());machine.execution().selectEngine(native);
        const auto actual=machine.execution().runToBoundary(8);reference.execution().run(1);
        check("Halfword/signed aborts retain exact fault state and suppress destination/base writeback",
            actual.steps==1&&actual.event==CpuStepEvent::DataAbort&&equal(machine,reference)&&native.stats().generatedSteps==1);
    }
    {
        Machine machine;machine.bus().write32(0,0xe3a00001);machine.bus().write32(4,0xe1c120b0);
        machine.state().r[1]=0;machine.state().r[2]=0x2a;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());machine.execution().selectEngine(native);
        machine.execution().run(1);machine.execution().run(1);machine.state().r[15]=0;machine.execution().run(1);
        check("Native halfword code stores invalidate the executing translation before reuse",machine.state().r[0]==42&&native.stats().memorySteps==1);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->state().r[15]=0x400;target->state().r[1]=0x3ff;target->state().r[2]=0x2aaa;
            target->bus().write32(0x400,0xe3a00001);target->bus().write32(0x404,0xe1c120b0);
            target->bus().write32(0x408,0xeafffffcu);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());machine.execution().selectEngine(native);
        machine.execution().run(7);reference.execution().run(7);
        check("An odd halfword store crossing into the active code page exits before a stale loop iteration",
            equal(machine,reference)&&machine.state().r[0]==42);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[1]=0x30001000;
                const uint32_t code[]={0xe3a00007,0xe3a02064,0xe5913000,0xe2833001,0xe5813000,0xe2522001,0x1afffffa};
                for(unsigned i=0;i<7;++i) target->bus().write32(i*4,code[i]);
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto deadline=machine.execution().time().ticks+11;
            const auto actual=machine.execution().run(129,deadline),expected=reference.execution().run(129,deadline);
            same=same&&actual==expected&&equal(machine,reference)&&
                machine.bus().read32(0x30001000)==reference.bus().read32(0x30001000);
        }
        check("Suffix RAM loops retain precise partial-iteration budgets and every legacy deadline phase",same);
    }
    {
        Machine machine,reference; bool same=true; size_t cases=0;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(unsigned kind=0;kind<4;++kind) for(unsigned amount:{0u,1u,31u}) for(bool carry:{false,true})
        for(bool load:{false,true}) for(bool byte:{false,true}) for(bool pre:{false,true}) for(bool up:{false,true}) {
            constexpr uint32_t source=0x80000003,address=0x30001001;
            uint32_t offset=0;
            if(kind==0) offset=source<<amount;
            if(kind==1) offset=amount?source>>amount:0;
            if(kind==2) offset=uint32_t(int32_t(source)>>(amount?amount:31));
            if(kind==3) offset=amount?(source>>amount)|(source<<(32-amount)):(source>>1)|(carry?0x80000000u:0);
            const uint32_t raw=0xe6000004u|(pre?1u<<24:0)|(up?1u<<23:0)|(byte?1u<<22:0)|
                (pre?1u<<21:0)|(load?1u<<20:0)|(1u<<16)|(2u<<12)|(amount<<7)|(kind<<5);
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0; target->state().cpsr=0xd3|(carry?FLAG_C:0);
                target->state().r[1]=pre?(up?address-offset:address+offset):address;
                target->state().r[2]=0x12345678; target->state().r[4]=source;
                target->bus().write32(0,raw); target->bus().write32(address&~3u,0x76543210);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equal(machine,reference)&&machine.bus().read32(address&~3u)==reference.bus().read32(address&~3u); ++cases;
        }
        check("Native register-offset byte/word transfers preserve every immediate shifter, carry, index and direction",
            same&&native.stats().generatedSteps==cases&&native.stats().fallbackSteps==0);
    }
    {
        bool same=true; uint64_t direct=0;
        for(unsigned region=0;region<4;++region) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->bus().write32(0x30004000,0x30008001);
                target->bus().write32(0x30008000,0x30000ff2);
                target->bus().write32(0x30008004,0x30001632); // AP fields: 3,0,2,1.
                target->bus().write32(0x30000000,0xe5912000);
                target->bus().write32(0x30001000+region*1024,0x12345678);
                target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000;
                target->state().cp15_dacr=1; target->state().cpsr=0x10;
                target->state().r[1]=0x1000+region*1024;
                target->execution().selectInterpreter(target->cpu());
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            for(Machine* target:{&machine,&reference}) {
                uint32_t physical=0; target->bus().setUserMode(false);
                target->bus().translateDataAddress(target->state().r[1],false,physical);
                target->bus().setUserMode(true);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equal(machine,reference); direct+=native.stats().inlineLoads;
        }
        check("Inline TLB reads enforce each small-page permission field after privileged cache priming",same&&direct==2);
    }
    {
        Machine machine,reference; bool same=true;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(uint32_t flags=0;flags<16;++flags) for(uint32_t pattern:{0u,0x7fffffffu,0x80000000u,0xffffffffu}) {
            for(Machine* target:{&machine,&reference}) {
                target->state().cpsr=0xd3|(flags<<28); target->state().r[15]=0;
                for(unsigned i=0;i<15;++i) target->state().r[i]=pattern+i;
                target->state().r[8]=0x30001000;
                const uint32_t code[]={0xe0910002,0x00a33004,0xe2505001,0x11a06005,0xe0d67002,
                    0xe5887000,0xe5989000,0xe029a001,0xe12fff1e};
                for(unsigned i=0;i<9;++i) target->bus().write32(i*4,code[i]);
            }
            machine.execution().run(8); reference.execution().run(8);
            same=same&&equal(machine,reference)&&machine.bus().read32(0x30001000)==reference.bus().read32(0x30001000);
        }
        check("Cached arithmetic flags, predication and carry dependencies materialize before RAM helpers",same);
    }
    {
        Machine machine,reference; bool same=true; size_t cases=0;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        for(bool byte:{false,true}) for(bool up:{false,true}) for(uint32_t alignment:{0u,1u,2u,3u}) {
            constexpr uint32_t pc=0x30008000;
            const uint32_t offset=64+alignment,address=up?pc+8+offset:pc+8-offset;
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=pc;
                target->bus().write32(address&~3u,0x87654321);
                target->bus().write32(pc,0xe51f2000u|(byte?1u<<22:0)|(up?1u<<23:0)|offset);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equal(machine,reference); ++cases;
        }
        check("Native literal loads use instruction PC plus eight with byte, rotated word and signed offsets",
            same&&native.stats().generatedSteps==cases&&native.stats().fallbackSteps==0);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02);
            target->bus().write32(0x30000000,0x05912000); target->bus().write32(0x30000004,0xe12fff1e);
            target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000; target->state().cp15_dacr=1;
            target->state().cpsr=0x10; target->state().r[1]=0x200000; target->state().r[2]=42;
            target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(1); reference.execution().run(1);
        const bool skipped=equal(machine,reference) && machine.cpu().getPC()==4 && native.stats().memorySteps==0;
        for(Machine* target:{&machine,&reference}) { target->state().r[15]=0; target->state().cpsr|=FLAG_Z; }
        const auto exit=machine.execution().runToBoundary(1); reference.execution().run(1);
        check("A failed memory condition cannot abort while the same cached operation faults when enabled",
            skipped && equal(machine,reference) && exit.event==CpuStepEvent::DataAbort &&
            machine.cpu().getReg(2)==42 && native.stats().memorySteps==1);
    }
    {
        Machine machine,reference;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        bool same=true;
        for(uint32_t condition=0;condition<15;++condition) for(uint32_t flags=0;flags<16;++flags)
        for(uint32_t operation:{0x05912000u,0x05812000u,0x04d12001u}) {
            for(Machine* target:{&machine,&reference}) {
                target->state().r[15]=0; target->state().r[1]=0x30001000; target->state().r[2]=0x12345678;
                target->state().cpsr=(flags<<28)|0xd3;
                target->bus().write32(0,(condition<<28)|operation); target->bus().write32(4,0xe12fff1e);
                target->bus().write32(0x30001000,0x87654321);
            }
            machine.execution().run(1); reference.execution().run(1);
            same=same&&equal(machine,reference)&&machine.bus().read32(0x30001000)==reference.bus().read32(0x30001000);
        }
        check("Conditional byte word and post-index accesses match scalar execution for every condition and NZCV",
            same && native.stats().generatedSteps==720 && native.stats().compilationFailures==0);
    }
    {
        Machine machine,reference;
        std::vector<int16_t> pcm,referencePcm;
        machine.bus().setAudioCallback([&](const int16_t* p,size_t n){pcm.insert(pcm.end(),p,p+n);});
        reference.bus().setAudioCallback([&](const int16_t* p,size_t n){referencePcm.insert(referencePcm.end(),p,p+n);});
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,3); target->state().r[1]=0x30010000;
            target->bus().write32(0,0xe5812000); target->bus().write32(4,0xe5913000);
            target->bus().write32(8,0xe2822001); target->bus().write32(12,0xeafffffb);
            for(unsigned i=0;i<16;++i) target->bus().write16(0x30010000+i*2,uint16_t(i*31));
            target->bus().write32(0x4b000080,0x30010000); target->bus().write32(0x4b000088,0x55000010);
            target->bus().write32(0x4b000090,16|(1u<<20)|(1u<<22)); target->bus().write32(0x4b0000a0,2);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(25000); reference.execution().run(25000);
        check("Mixed RAM blocks preserve PCM samples while repeatedly updating DMA source memory",
            equal(machine,reference) && pcm==referencePcm && pcm.size()==16 &&
            machine.bus().getMmio(0x4b000098)==reference.bus().getMmio(0x4b000098) &&
            machine.bus().getMmio(0x4b000094)==reference.bus().getMmio(0x4b000094));
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[1]=0x30001000;
                for(unsigned i=0;i<32;++i) target->bus().write32(i*4,(i&1)?0xe5913000:0xe5812000);
            }
            const auto deadline=machine.execution().time().ticks+2;
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto exit=machine.execution().runToBoundary(32,deadline); reference.execution().run(exit.steps,deadline);
            same=same&&exit.reason==ExecutionExitReason::Deadline && equal(machine,reference) &&
                machine.execution().time().ticks==deadline && native.stats().memorySteps==exit.steps;
        }
        check("Batched RAM time stops at the exact external deadline for every legacy phase",same);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[1]=0x30001000;
                const uint32_t code[]={0xe3a02001,0xe5812000,0xe5913000,0xe2833029,0xe5c13004,0xe5d14004,0xeafffff8};
                for(unsigned i=0;i<7;++i) target->bus().write32(i*4,code[i]);
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto exit=machine.execution().runToBoundary(7); reference.execution().run(7);
            same=same&&exit.steps==7 && equal(machine,reference) && machine.cpu().getReg(4)==42 &&
                machine.bus().read32(0x30001000)==reference.bus().read32(0x30001000) &&
                machine.bus().read8(0x30001004)==reference.bus().read8(0x30001004) &&
                native.stats().memorySteps==4 && native.stats().compilations==1;
            machine.execution().selectInterpreter(machine.cpu()); machine.execution().run(5); reference.execution().run(5);
            same=same&&equal(machine,reference);
        }
        check("Several dependent RAM accesses share one native block with exact phase and handoff",same);
    }
    for(bool mirror:{false,true}) {
        Machine machine,reference; const uint32_t pc=mirror?0x30008000u:0;
        for(Machine* target:{&machine,&reference}) {
            target->state().r[15]=pc; target->state().r[0]=7;
            target->state().r[1]=pc+4+(mirror?0x02000000u:0); target->state().r[2]=0xe3a0002a;
            target->bus().write32(pc,0xe5812000); target->bus().write32(pc+4,0xe3a00001);
            target->bus().write32(pc+8,0xe12fff1e);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto first=machine.execution().runToBoundary(32); reference.execution().run(1);
        const bool stopped=first.steps==1 && equal(machine,reference) && machine.cpu().getReg(0)==7;
        const auto second=machine.execution().runToBoundary(32); reference.execution().run(1);
        check("An in-block code write through SRAM or a RAM mirror stops before stale following code",
            stopped && second.steps==1 && equal(machine,reference) && machine.cpu().getReg(0)==42);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02);
            target->bus().write32(0x30000000,0xe5812000); target->bus().write32(0x30000004,0xe5943000);
            target->bus().write32(0x30000008,0xe3a05063); target->bus().write32(0x3000000c,0xe12fff1e);
            target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000; target->state().cp15_dacr=1;
            target->state().cpsr=0x10; target->state().r[1]=0x1000; target->state().r[2]=42;
            target->state().r[4]=0x200000; target->state().r[5]=7;
            target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32); reference.execution().run(2);
        check("A second-access abort retains the first store and skips later integer work",
            exit.steps==2 && exit.event==CpuStepEvent::DataAbort && equal(machine,reference) &&
            machine.cpu().getLR()==12 && machine.cpu().getReg(5)==7 && machine.bus().read32Phys(0x30001000)==42 &&
            native.stats().memorySteps==2);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->state().r[1]=0x30001000; target->state().r[2]=1; target->state().r[4]=0x58000000;
            target->bus().write32(0,0xe5812000); target->bus().write32(4,0xe5842000);
            target->bus().write32(8,0xe3a03063); target->bus().write32(12,0xe12fff1e);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32); reference.execution().run(2);
        check("A device access after RAM ends the block before later guest work",
            exit.steps==2 && equal(machine,reference) && machine.cpu().getReg(3)==0 &&
            machine.bus().ticksUntilEvent()==reference.bus().ticksUntilEvent() && native.stats().memorySteps==2);
    }
    {
        bool same=true;
        for(size_t ratio:{1u,3u,6u,16u}) for(size_t phase=0;phase<ratio;++phase) {
            Machine machine,reference;
            for(Machine* target:{&machine,&reference}) {
                target->configureExecution(false,ratio); target->execution().run(phase);
                target->state().r[15]=0; target->state().r[1]=0x58000000;
                target->bus().write32(0,0xe3a02001); // MOV r2,#1
                target->bus().write32(4,0xe2833001); // ADD r3,r3,#1
                target->bus().write32(8,0xe5812000); // STR r2,[r1], starts ADC.
            }
            X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
            const auto exit=machine.execution().runToBoundary(32); reference.execution().run(3);
            same=same&&exit.steps==3 && equal(machine,reference) && native.stats().generatedSteps==3 &&
                native.stats().memorySteps==1 && native.stats().compilations==1 &&
                machine.bus().ticksUntilEvent()==reference.bus().ticksUntilEvent();
            machine.execution().selectInterpreter(machine.cpu());
            machine.execution().run(3); reference.execution().run(3);
            same=same&&equal(machine,reference);
        }
        check("Integer prefix and terminal MMIO preserve every legacy phase device deadline and handoff",same);
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->configureExecution(false,3); target->state().r[1]=0x58000000;
            target->bus().write32(0,0xe3a02001); target->bus().write32(4,0xe2833001); target->bus().write32(8,0xe5812000);
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32,1); reference.execution().run(1,1);
        check("A deadline inside an integer prefix stops before the later memory side effect",
            exit.steps==1 && equal(machine,reference) && native.stats().memorySteps==0 &&
            machine.bus().getMmio(0x58000000)==reference.bus().getMmio(0x58000000));
    }
    {
        Machine machine,reference;
        for(Machine* target:{&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02);
            target->bus().write32(0x30000000,0xe3a02063); target->bus().write32(0x30000004,0xe2833001);
            target->bus().write32(0x30000008,0xe5912000);
            target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000; target->state().cp15_dacr=1;
            target->state().cpsr=0x10; target->state().r[1]=0x200000;
            target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        const auto exit=machine.execution().runToBoundary(32); reference.execution().run(3);
        check("A terminal load abort retains integer-prefix results and attributes the exact faulting PC",
            exit.steps==3 && exit.event==CpuStepEvent::DataAbort && equal(machine,reference) &&
            machine.cpu().getReg(2)==99 && machine.cpu().getLR()==16 && native.stats().memorySteps==1);
    }
    {
        Machine machine,reference; X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
        machine.configureExecution(false,3); reference.configureExecution(false,3); machine.execution().selectEngine(native);
        bool same=true; size_t cases=0;
        for (uint32_t instruction : {0xe5912000u,0xe5912001u,0xe5912002u,0xe5912003u,0xe5812001u,
                                    0xe5d12001u,0xe5c12002u,0xe5b12004u,0xe5312004u,0xe4912004u,
                                    0xe4012004u,0xe5a12004u,0xe5212004u}) {
            for (Machine* target : {&machine,&reference}) {
                target->state().r[15]=0; target->state().r[1]=0x30001008; target->state().r[2]=0x123456ab;
                target->bus().write32(0,instruction);
                target->bus().write32(4,0xe12fff1e); // Unsupported BX ends this fixture before execution.
                for(unsigned offset=0;offset<24;++offset)target->bus().write8(0x30001000+offset,uint8_t(offset+1));
            }
            auto exit=machine.execution().runToBoundary(32); reference.execution().run(1);
            same=same&&exit.steps==1 && equal(machine,reference);
            for(unsigned offset=0;offset<24;++offset)same=same&&machine.bus().read8(0x30001000+offset)==reference.bus().read8(0x30001000+offset);
            ++cases;
        }
        check("Checked native byte/word access matches scalar alignment addressing writeback and tick phase",
            same && native.stats().memorySteps==cases && native.stats().compilationFailures==0 && native.stats().fallbackSteps==0);
    }
    for (bool store : {false,true}) {
        Machine machine,reference;
        for(Machine* target : {&machine,&reference}) {
            target->bus().write32(0x30004000,0x30000c02);
            target->bus().write32(0x30004004,0x30100402);
            target->bus().write32(0x30000000,store?0xe5a12004:0xe5b12004);
            target->state().cp15_control|=1; target->state().cp15_ttb=0x30004000; target->state().cp15_dacr=1;
            target->state().cpsr=0x10; target->state().r[1]=store?0x100000:0x200000;
            target->execution().selectInterpreter(target->cpu());
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        auto exit=machine.execution().runToBoundary(32); reference.execution().run(1);
        check("Checked native fault preserves destination and base while materializing abort state",
            exit.steps==1 && exit.event==CpuStepEvent::DataAbort && equal(machine,reference) &&
            machine.cpu().getPC()==16 && machine.cpu().getLR()==8 && native.stats().memorySteps==1);
    }
    {
        Machine machine,reference;
        for(Machine* target : {&machine,&reference}) {
            target->bus().write32(0,0xe5812000); target->state().r[1]=0x58000000; target->state().r[2]=1;
        }
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        auto exit=machine.execution().runToBoundary(32); reference.execution().run(1);
        check("MMIO store ends its batch and arms the same device deadline",
            exit.steps==1 && equal(machine,reference) && machine.bus().ticksUntilEvent()==reference.bus().ticksUntilEvent() &&
            machine.bus().getMmio(0x58000000)==reference.bus().getMmio(0x58000000));
    }
    {
        Machine machine; machine.bus().write32(0,0xe3a00001); machine.bus().write32(4,0xe5812000);
        machine.state().r[1]=0; machine.state().r[2]=0xe3a0002a;
        X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus()); machine.execution().selectEngine(native);
        machine.execution().run(1); machine.execution().run(32); machine.state().r[15]=0; machine.execution().run(1);
        check("Native store to compiled code is observed before cache reuse",machine.cpu().getReg(0)==42 && native.stats().memorySteps==1);
    }
    std::cout<<failures<<" failure(s)\n"; return failures?1:0;
}
