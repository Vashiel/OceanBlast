#pragma once
#include "cpu/arm920t.h"
#include "emulation_clock.h"
#include <cstddef>
#include <cstdint>

namespace oceanblast {
inline uint64_t legacyStepsUntilTickLimit(uint64_t remainingTicks, size_t ratio, size_t phase) {
    return remainingTicks ? (remainingTicks - 1) * ratio + (phase ? ratio - phase : 0) + 1 : 0;
}
// Batch host bookkeeping only. Every guest instruction still uses CPU::step,
// including its normal interrupt, fault and peripheral handling.
inline size_t runLegacyInstructions(ARM920T& cpu, size_t budget, size_t ratio,
                                   size_t& phase, uint64_t& guestTicks) {
    size_t retired = 0;
    for (; retired < budget && !cpu.isHalted(); ++retired) {
        const bool advance = phase == 0;
        cpu.step(advance ? 1 : 0);
        guestTicks += advance;
        if (++phase == ratio) phase = 0;
    }
    return retired;
}

inline size_t runClockedInstructions(ARM920T& cpu, Bus& bus, EmulationClock& clock,
                                    size_t budget, uint64_t stopTicks, uint64_t& guestTicks) {
    size_t retired = 0;
    while (retired < budget && !cpu.isHalted() && !cpu.isWaitingForInterrupt() && guestTicks < stopTicks) {
        const u32 frequency = cpu.getExecutionClock();
        cpu.step(0);
        bus.tick(clock.advance(cpu.getLastCycles(), frequency));
        guestTicks = clock.elapsedTicks();
        ++retired;
    }
    return retired;
}
}
