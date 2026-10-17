#include "core/machine.h"
#include <iostream>
using namespace oceanblast;
static int failures=0;
static void check(const char* name,bool ok) {
    std::cout << (ok?"PASS ":"FAIL ") << name << '\n';
    failures+=!ok;
}
static void program(Bus& bus) {
    bus.write32(0,0xe2800001); // ADD r0,r0,#1
    bus.write32(4,0xeafffffd); // B 0
}
// An observable wrapper verifies generic batch dispatch using real interpreter
// execution. It is a test adapter, not a simulated native backend.
class ObservedEngine final : public ExecutionEngine {
public:
    InterpreterEngine interpreter;
    size_t entries=0,batches=0;
    bool busy=false;
    ObservedEngine(ARM920T& cpu,Bus& bus) : interpreter(cpu,bus) {}
    const ARM920TState& state() const override { return interpreter.state(); }
    const Bus& memory() const override { return interpreter.memory(); }
    bool canHandoff() const override { return !busy && interpreter.canHandoff(); }
    void enter(bool clocks) override { ++entries; interpreter.enter(clocks); }
    ExecutionExit runToBoundary(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) override {
        ++batches; return interpreter.runToBoundary(timeline,budget,stopTicks);
    }
};
int main() {
    {
        Machine machine,reference;
        program(machine.bus()); program(reference.bus());
        machine.configureExecution(false,3); reference.configureExecution(false,3);
        machine.execution().run(2); reference.execution().run(2);
        ObservedEngine engine(machine.cpu(),machine.bus());
        machine.execution().selectEngine(engine);
        const size_t steps=machine.execution().run(17);
        reference.execution().run(17);
        check("Generic engine dispatch preserves the shared timeline and program",
            engine.entries==1 && engine.batches==1 && steps==17 &&
            machine.cpu().getReg(0)==reference.cpu().getReg(0) && machine.cpu().getPC()==reference.cpu().getPC() &&
            machine.execution().time().ticks==reference.execution().time().ticks);
        engine.busy=true;
        bool rejected=false;
        try { machine.execution().selectInterpreter(machine.cpu()); } catch(const std::logic_error&) { rejected=true; }
        check("Unsafe engine handoff is rejected before changing selection",rejected && engine.entries==1);
        engine.busy=false;
        machine.execution().selectInterpreter(machine.cpu());
        machine.execution().run(1); reference.execution().run(1);
        check("Interpreter fallback resumes at the exact next guest step",
            machine.cpu().getReg(0)==reference.cpu().getReg(0) && machine.cpu().getPC()==reference.cpu().getPC() &&
            machine.execution().time().ticks==reference.execution().time().ticks);
    }
    for (size_t ratio : {1u,3u,6u,16u}) {
        Machine reference,machine;
        program(reference.bus()); program(machine.bus());
        reference.configureExecution(false,ratio); machine.configureExecution(false,ratio);
        ARM920T alternate(machine.bus(),machine.state());
        for (size_t count : {1u,2u,17u,3u,49u}) {
            reference.execution().run(count);
            machine.execution().selectInterpreter(alternate);
            auto exit=machine.execution().runToBoundary(count);
            check("Handoff preserves legacy ratio phase and bounded program state",
                exit.reason==ExecutionExitReason::Budget && exit.steps==count && !exit.event &&
                machine.cpu().getPC()==reference.cpu().getPC() &&
                machine.cpu().getReg(0)==reference.cpu().getReg(0) &&
                machine.execution().time().ticks==reference.execution().time().ticks);
            machine.execution().selectInterpreter(machine.cpu());
        }
    }
    {
        Machine reference,machine;
        program(reference.bus()); program(machine.bus());
        // Use a non-integral peripheral/CPU clock ratio to exercise the fraction.
        for (Bus* bus : {&reference.bus(),&machine.bus()}) {
            bus->write32(0x4c000004,0x0005c080);
        }
        reference.state().cp15_control|=1u<<30;
        machine.state().cp15_control|=1u<<30;
        reference.configureExecution(true,1); machine.configureExecution(true,1);
        ARM920T alternate(machine.bus(),machine.state());
        for (size_t count : {1u,7u,2u,101u,3u}) {
            reference.execution().run(count);
            machine.execution().selectInterpreter(alternate);
            auto exit=machine.execution().runToBoundary(count);
            check("Handoff preserves fractional clock accumulation",
                exit.steps==count && machine.cpu().getPC()==reference.cpu().getPC() &&
                machine.execution().time().ticks==reference.execution().time().ticks);
            machine.execution().selectInterpreter(machine.cpu());
        }
    }
    {
        Machine machine;
        auto& state=machine.state();
        state.r[0]=123; state.r13_irq=456; state.spsr_irq=0x60000010;
        state.fpa[3]={2,0x12345678,0x87654321}; state.fpaFpsr=0x01000000;
        state.cp15_far=0xfeed0000; state.waitingForInterrupt=true;
        ARM920T alternate(machine.bus(),state);
        machine.execution().selectInterpreter(alternate);
        auto exit=machine.execution().runToBoundary(10);
        check("Binding preserves banked FPA fault and wait state without reset",
            alternate.getReg(0)==123 && state.r13_irq==456 && state.spsr_irq==0x60000010 &&
            state.fpa[3].w1==0x87654321 && state.fpaFpsr==0x01000000 &&
            state.cp15_far==0xfeed0000 && exit.reason==ExecutionExitReason::Waiting && exit.steps==0);
        state.halted=true;
        check("Halted exit consumes no guest steps",machine.execution().runToBoundary(10).reason==ExecutionExitReason::Halted);
        machine.execution().selectInterpreter(machine.cpu());
        Machine other;
        bool rejected=false;
        try { machine.execution().selectInterpreter(other.cpu()); } catch(const std::invalid_argument&) { rejected=true; }
        check("Cross-machine handoff is rejected without changing selected state",rejected && machine.cpu().getReg(0)==123);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xef000000); // SWI
        auto exit=machine.execution().runToBoundary(20);
        check("Supervisor call exits before executing its vector",
            exit.steps==1 && exit.reason==ExecutionExitReason::CpuEvent &&
            exit.event==ARM920T::StepEvent::SupervisorCall && machine.cpu().getPC()==8 && machine.cpu().getLR()==4);
        machine.cpu().reset();
        machine.cpu().setFpa11Execution(false);
        machine.bus().write32(0,0xee000190); // Unsupported FPA instruction
        exit=machine.execution().runToBoundary(20);
        check("Undefined instruction exposes precise exception entry",
            exit.steps==1 && exit.event==ARM920T::StepEvent::UndefinedInstruction && machine.cpu().getPC()==4);
    }
    {
        Machine machine;
        program(machine.bus()); machine.configureExecution(false,3);
        auto exit=machine.execution().runToBoundary(100,2);
        check("Tick deadline bounds legacy work and returns elapsed time",
            exit.reason==ExecutionExitReason::Deadline && exit.steps==4 && exit.ticks==2 && machine.cpu().getReg(0)==2);
        exit=machine.execution().runToBoundary(100,1);
        check("Expired deadline consumes no work or ticks",exit.steps==0 && exit.ticks==0 && !exit.event);
    }
    {
        Machine machine;
        auto& state=machine.state();
        machine.bus().write32(0x30004000,0x30000c02); // VA 0 section -> SDRAM
        machine.bus().write32(0x30000000,0xe3a0002a); // MOV r0,#42
        state.cp15_control|=1; state.cp15_ttb=0x30004000; state.cp15_dacr=3;
        ARM920T alternate(machine.bus(),state);
        machine.execution().selectInterpreter(alternate);
        auto exit=machine.execution().runToBoundary(1);
        check("Engine entry restores CP15 mapping before instruction fetch",
            exit.steps==1 && state.r[0]==42 && state.r[15]==4 && machine.bus().isMmuEnabled());
        state.r[15]=0x00200000;
        exit=machine.execution().runToBoundary(10);
        check("Prefetch fault exits at the abort vector",
            exit.steps==1 && exit.event==ARM920T::StepEvent::PrefetchAbort && state.r[15]==12 && state.r[14]==0x00200004);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xe12fff13); // BX r3
        machine.bus().write16(0x100,0x2007); // MOVS r0,#7
        machine.cpu().setReg(3,0x101);
        auto exit=machine.execution().runToBoundary(20);
        check("ARM to Thumb transition exposes a context boundary",
            exit.reason==ExecutionExitReason::ContextChange && exit.steps==1 && machine.cpu().getPC()==0x100 && machine.cpu().isThumb());
        ARM920T alternate(machine.bus(),machine.state());
        machine.execution().selectInterpreter(alternate);
        exit=machine.execution().runToBoundary(1);
        check("Thumb state resumes through another execution view",
            exit.steps==1 && machine.cpu().getPC()==0x102 && machine.cpu().getReg(0)==7);
    }
    {
        Machine machine;
        machine.bus().write32(0x30004000,0x30000c02);
        machine.bus().write32(0x30000000,0xe5910000); // LDR r0,[r1]
        machine.state().cp15_control|=1; machine.state().cp15_ttb=0x30004000; machine.state().cp15_dacr=3;
        machine.state().r[1]=0x00200000;
        machine.execution().selectInterpreter(machine.cpu());
        auto exit=machine.execution().runToBoundary(20);
        check("Data fault materializes fault address and abort return PC",
            exit.steps==1 && exit.event==ARM920T::StepEvent::DataAbort &&
            machine.cpu().getPC()==16 && machine.cpu().getLR()==8 && machine.state().cp15_far==0x00200000);
    }
    {
        Machine machine;
        machine.cpu().setReg(13,0x1110); machine.cpu().setReg(14,0x2220);
        machine.bus().write32(0,0xe121f000); // MSR CPSR_c,r0
        machine.cpu().setReg(0,0x12);
        machine.execution().runToBoundary(1);
        ARM920T alternate(machine.bus(),machine.state());
        machine.execution().selectInterpreter(alternate);
        alternate.setReg(13,0x3330); alternate.setReg(14,0x4440);
        alternate.setReg(0,0x13); alternate.setReg(15,0);
        machine.execution().runToBoundary(1);
        check("Mode bank restoration survives execution-view handoff",
            machine.cpu().getSP()==0x1110 && machine.cpu().getLR()==0x2220 &&
            machine.state().r13_irq==0x3330 && machine.state().r14_irq==0x4440);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xe5810000); // STR r0,[r1], arm timer4
        machine.bus().write32(4,0xeafffffe); // B 4
        machine.cpu().setReg(0,(1u<<20)|(1u<<21));
        machine.cpu().setReg(1,0x51000008);
        machine.bus().write32(0x5100003c,2);
        auto exit=machine.execution().runToBoundary(10000);
        check("Newly armed timer shortens the execution deadline",
            exit.reason==ExecutionExitReason::Deadline && exit.steps<10000 && machine.bus().getTimer4Expirations()==1);
    }
    {
        Machine machine;
        machine.bus().write32(0,0xee070f90); // WFI
        machine.configureExecution(true,1);
        auto exit=machine.execution().runToBoundary(20);
        check("WFI returns without spinning through the step budget",
            exit.reason==ExecutionExitReason::Waiting && exit.steps==1 && machine.cpu().getPC()==4);
        machine.state().cpsr &= ~FLAG_I;
        machine.bus().write32(0x4a000008,~(1u<<14));
        machine.bus().write32(0x5100003c,1);
        machine.bus().write32(0x51000008,(1u<<20)|(1u<<21));
        machine.bus().tick(machine.bus().ticksUntilEvent());
        ARM920T alternate(machine.bus(),machine.state());
        machine.execution().selectInterpreter(alternate);
        exit=machine.execution().runToBoundary(20);
        check("Pending interrupt survives handoff and exits before vector execution",
            exit.steps==1 && exit.event==ARM920T::StepEvent::Interrupt && machine.cpu().getPC()==24 && !machine.state().waitingForInterrupt);
    }
    std::cout << failures << " failure(s)\n";
    return failures?1:0;
}
