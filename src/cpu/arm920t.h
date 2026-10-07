#pragma once
#include "../core/types.h"
#include "../memory/bus.h"
#include <string>

namespace oceanblast {

constexpr u32 FLAG_N = 1u << 31; // Negative
constexpr u32 FLAG_Z = 1u << 30; // Zero
constexpr u32 FLAG_C = 1u << 29; // Carry
constexpr u32 FLAG_V = 1u << 28; // Overflow
constexpr u32 FLAG_I = 1u << 7;  // IRQ disable
constexpr u32 FLAG_F = 1u << 6;  // FIQ disable
constexpr u32 FLAG_T = 1u << 5;  // Thumb state (1 = Thumb, 0 = ARM)

class ARM920T {
public:
    explicit ARM920T(Bus& bus);
    ~ARM920T();

    void reset(u32 startAddress = 0x00000000);
    void step();

    // Register Access
    u32 getReg(int index) const { return (index >= 0 && index < 16) ? r[index] : 0; }
    void setReg(int index, u32 val) { if (index >= 0 && index < 16) r[index] = val; }

    u32 getPC() const { return r[15]; }
    u32 getLR() const { return r[14]; }
    u32 getSP() const { return r[13]; }
    u32 getCPSR() const { return cpsr; }

    bool isThumb() const { return (cpsr & FLAG_T) != 0; }
    bool isHalted() const { return halted; }

    void dumpState() const;

    // Disassembler for tracing
    std::string disassembleCurrentARM() const;

private:
    Bus& bus;
    u32 r[16];   // r0..r15 (r13=SP, r14=LR, r15=PC)
    u32 cpsr;
    u32 spsr;
    bool halted;

    // Coprocessor 15 (System Control Coprocessor)
    u32 cp15_control = 0x00000070; // Control Register (c1)
    u32 cp15_ttb     = 0;          // Translation Table Base (c2)
    u32 cp15_dacr    = 0;          // Domain Access Control (c3)

    // Helper functions
    bool evaluateCondition(u32 cond) const;
    void setNZFlags(u32 result);
    void setAddFlags(u32 a, u32 b, u32 res);
    void setSubFlags(u32 a, u32 b, u32 res);
    u32 shiftOperand(u32 value, u32 shiftType, u32 shiftAmount, bool& carryOut);

    // ARM Execution
    void stepARM();
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
    void executeSWI(u32 instr);

    // Thumb Execution
    void stepThumb();
};

} // namespace oceanblast
