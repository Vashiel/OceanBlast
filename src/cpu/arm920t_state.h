#pragma once
#include "../core/types.h"

namespace oceanblast {

constexpr u32 FLAG_N = 1u << 31;
constexpr u32 FLAG_Z = 1u << 30;
constexpr u32 FLAG_C = 1u << 29;
constexpr u32 FLAG_V = 1u << 28;
constexpr u32 FLAG_I = 1u << 7;
constexpr u32 FLAG_F = 1u << 6;
constexpr u32 FLAG_T = 1u << 5;

// Guest-visible CPU state, independent of the bus and instruction interpreter.
// This is an in-memory representation, not a portable save-state file format.
struct ARM920TState {
    u32 r[16]{};
    u32 cpsr = 0x000000D3;
    u32 spsr = 0;

    u32 r13_usr = 0, r14_usr = 0;
    u32 r13_svc = 0, r14_svc = 0, spsr_svc = 0;
    u32 r13_irq = 0, r14_irq = 0, spsr_irq = 0;
    u32 r13_abt = 0, r14_abt = 0, spsr_abt = 0;
    u32 r13_und = 0, r14_und = 0, spsr_und = 0;

    u32 cp15_control = 0x00000070;
    u32 cp15_ttb = 0;
    u32 cp15_dacr = 0;
    u32 cp15_fsr = 0;
    u32 cp15_far = 0;

    struct FpaReg {
        u8 type = 0; // 0: empty, 1: single, 2: double
        u32 w0 = 0;  // Single bits or double low word.
        u32 w1 = 0;  // Double high word.
    };
    FpaReg fpa[8]{};
    u32 fpaFpsr = 0;
    bool halted = false;
    bool waitingForInterrupt = false;
};

} // namespace oceanblast
