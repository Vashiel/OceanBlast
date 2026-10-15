#pragma once
#include "cpu/arm920t.h"
#include "emulation_clock.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace oceanblast {
inline uint64_t legacyStepsUntilTickLimit(uint64_t remainingTicks, size_t ratio, size_t phase) {
    return remainingTicks ? (remainingTicks - 1) * ratio + (phase ? ratio - phase : 0) + 1 : 0;
}
// Interpreter execution boundary. Single-step and batch callers share the same
// time advancement; instruction, interrupt and fault handling stays in CPU::step.
class InterpreterExecution {
public:
    InterpreterExecution(ARM920T& cpu, Bus& bus) : cpu(cpu), bus(bus) {}

    void stepLegacy(size_t ratio, size_t& phase, uint64_t& guestTicks) {
        const bool advance = phase == 0;
        cpu.step(advance ? 1 : 0);
        guestTicks += advance;
        if (++phase == ratio) phase = 0;
    }

    void stepClocked(EmulationClock& clock, uint64_t& guestTicks) {
        const u32 frequency = cpu.getExecutionClock();
        cpu.step(0);
        bus.tick(clock.advance(cpu.getLastCycles(), frequency));
        guestTicks = clock.elapsedTicks();
    }

    size_t runLegacy(size_t budget, size_t ratio, size_t& phase, uint64_t& guestTicks) {
        size_t retired = 0;
        while (retired < budget && !cpu.isHalted()) {
            if (phase == 0) {
                cpu.step(1);
                ++guestTicks;
                ++retired;
                phase = ratio == 1 ? 0 : 1;
            }
            if (phase == 0) continue;
            const size_t count = std::min(budget-retired,ratio-phase);
            size_t completed = 0;
            for (; completed < count && !cpu.isHalted(); ++completed) cpu.step(0);
            retired += completed;
            phase += completed;
            if (phase == ratio) phase = 0;
        }
        return retired;
    }

    size_t runClocked(EmulationClock& clock, size_t budget, uint64_t stopTicks, uint64_t& guestTicks) {
        size_t retired = 0;
        while (retired < budget && !cpu.isHalted() && !cpu.isWaitingForInterrupt() && guestTicks < stopTicks) {
            stepClocked(clock, guestTicks);
            ++retired;
        }
        return retired;
    }

private:
    ARM920T& cpu;
    Bus& bus;
};
}
