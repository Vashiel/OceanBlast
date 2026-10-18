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
