#pragma once
#include "arm920t_state.h"
#include "../memory/bus.h"
#include <string>
#include <memory>

namespace oceanblast {

class ARM920T {
public:
    explicit ARM920T(Bus& bus);
    // Attach an execution view without resetting machine-owned architectural state.
    ARM920T(Bus& bus, ARM920TState& sharedState);
    ARM920T(const ARM920T&) = delete;
    ARM920T& operator=(const ARM920T&) = delete;
    ARM920T(ARM920T&&) = delete;
    ARM920T& operator=(ARM920T&&) = delete;
    ~ARM920T();

    enum class StepEvent { Instruction, Interrupt, PrefetchAbort, DataAbort,
                           UndefinedInstruction, SupervisorCall, Waiting, Halted, LegacyAcceleration };
    StepEvent getLastEvent() const { return lastEvent; }
    bool sharesContext(const ARM920T& other) const { return &state == &other.state && &bus == &other.bus; }
    bool usesBus(const Bus& candidate) const { return &bus == &candidate; }
    bool canHandoff() const { return acceleratedBlitStall == 0; }
    // Reconcile derived memory context on entry; preserve guest wait and timing state.
    void enterExecution(bool registerClocks);

    void reset(u32 startAddress = 0x00000000);
    // Diagnostic clock-ratio experiments can execute with zero peripheral ticks.
    // Normal execution retains one peripheral tick per step.
    void step(size_t peripheralTicks = 1);
    void setSimpleAluExecution(bool enabled) { simpleAluExecution = enabled; }
    void setFpa11Execution(bool enabled) { fpa11Execution = enabled; }
    void setCycleTiming(bool enabled) { cycleTiming = enabled; state.waitingForInterrupt = false; }
    uint32_t getControlRegister() const { return state.cp15_control; }
    uint32_t getExecutionClock() const {
        return (state.cp15_control & (1u << 30)) ? bus.getCpuClock() : bus.getBusClock();
    }
    uint32_t getLastCycles() const { return lastCycles; }
    bool isWaitingForInterrupt() const { return state.waitingForInterrupt; }
    void setDebugLogging(bool enabled) { debugLogging = enabled; }
    void setFaultLogging(bool enabled) { faultLogging = enabled; }
    // Observe a userspace SWI before exception entry; disabled in normal execution.
    using SyscallObserver = std::function<void(u32, u32, const std::array<u32, 6>&)>;
    void setSyscallObserver(SyscallObserver observer) { syscallObserver = std::move(observer); }

    // Register Access
    u32 getReg(int index) const { return (index >= 0 && index < 16) ? state.r[index] : 0; }
    void setReg(int index, u32 val) { if (index >= 0 && index < 16) state.r[index] = val; }

    u32 getPC() const { return state.r[15]; }
    u32 getLR() const { return state.r[14]; }
    u32 getSP() const { return state.r[13]; }
    u32 getCPSR() const { return state.cpsr; }

    bool isThumb() const { return (state.cpsr & FLAG_T) != 0; }
    bool isHalted() const { return state.halted; }

    // Read-only view; restoring CP15 state also requires bus synchronization.
    const ARM920TState& getState() const { return state; }
    void dumpState() const;

    // Disassembler for tracing
    std::string disassembleCurrentARM() const;

private:
    Bus& bus;
    std::unique_ptr<ARM920TState> ownedState;
    ARM920TState& state;
    StepEvent lastEvent = StepEvent::Instruction;
    bool simpleAluExecution = true;
    bool fpa11Execution = true;
    using FpaReg = ARM920TState::FpaReg;
    float readFpaSingle(u32 reg) const;
    double readFpaDouble(u32 reg) const;
    void writeFpaSingle(u32 reg, float val);
    void writeFpaDouble(u32 reg, double val);
    bool executeFPA11DataTransfer(u32 instruction);
    bool executeFPA11Operation(u32 instruction);
    bool executeSimpleDataProcessing(u32 instruction);
    bool cycleTiming = false;
    uint32_t lastCycles = 1;
    uint32_t acceleratedBlitStall = 0;
    bool debugLogging = false;
    bool faultLogging = false;
    SyscallObserver syscallObserver;
    void logFaultContext(const char* kind, u32 instructionPC, u32 faultAddress);

    void switchMode(u32 newMode);
    void handleIrq();
    void handlePrefetchAbort(u32 faultPC);
    void handleDataAbort(u32 faultAddr, Bus::MmuFault faultType);
    void handleUndefinedInstruction(u32 instr);
    bool accelerateTitleBlit(size_t peripheralTicks);

    // Helper functions
    bool evaluateCondition(u32 cond) const;
    void setNZFlags(u32 result);
    void setAddFlags(u32 a, u32 b, u32 res);
    void setSubFlags(u32 a, u32 b, u32 res);
    u32 shiftOperand(u32 value, u32 shiftType, u32 shiftAmount, bool& carryOut, bool immediate = false);

    // ARM Execution
    void stepARM(u32 instruction);
    void executeBranch(u32 instr);
    void executeBX(u32 instr);
    void executeMRS(u32 instr);
    void executeMSR(u32 instr);
    void executeCP15(u32 instr);
    void executeDataProcessing(u32 instr);
    void executeSingleDataTransfer(u32 instr);
    void executeHalfwordTransfer(u32 instr);
    void executeBlockDataTransfer(u32 instr);
    void executeMultiply(u32 instr);
    void executeSwap(u32 instr);
    void executeSWI(u32 instr);

    // Thumb Execution
    void stepThumb(u16 instruction);
};

} // namespace oceanblast
