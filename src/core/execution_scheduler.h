#pragma once
#include "execution_batch.h"
#include <algorithm>

namespace oceanblast {

struct ExecutionTime {
    uint64_t ticks = 0;
    uint64_t idleTicks = 0;
};

// Owns time advancement for one machine. Host pacing and presentation remain
// outside this scheduler. Deadlines are observed between guest instructions.
class ExecutionScheduler {
public:
    ExecutionScheduler(ARM920T& cpu, Bus& bus, bool registerClocks, size_t ratio)
        : cpu(cpu), bus(bus), interpreter(cpu, bus), registerClocks(registerClocks), ratio(ratio) {}

    const ExecutionTime& time() const { return elapsed; }

    void step() {
        if (registerClocks) interpreter.stepClocked(clock, elapsed.ticks);
        else interpreter.stepLegacy(ratio, phase, elapsed.ticks);
    }

    size_t run(size_t budget, uint64_t stopTicks = UINT64_MAX) {
        if (!budget || cpu.isHalted() || elapsed.ticks >= stopTicks) return 0;
        if (registerClocks) {
            const uint64_t deadline = elapsed.ticks +
                std::min(bus.ticksUntilEvent(), stopTicks - elapsed.ticks);
            // A pending interrupt must be allowed to wake a waiting CPU.
            size_t retired = 0;
            if (cpu.isWaitingForInterrupt() && bus.hasPendingIrq()) {
                step();
                retired = 1;
            }
            return retired + interpreter.runClocked(clock, budget - retired, deadline, elapsed.ticks);
        }
        const uint64_t remaining = stopTicks - elapsed.ticks;
        // At most one peripheral tick elapses per instruction. Only form the
        // ratio product when the deadline can be reached within this budget.
        if (remaining <= budget)
            budget = std::min<uint64_t>(budget, legacyStepsUntilTickLimit(remaining, ratio, phase));
        return interpreter.runLegacy(budget, ratio, phase, elapsed.ticks);
    }

    uint64_t advanceIdle(uint64_t stopTicks) {
        if (!registerClocks || cpu.isHalted() || !cpu.isWaitingForInterrupt() ||
            bus.hasPendingIrq() || elapsed.ticks >= stopTicks) return 0;
        const uint64_t amount = std::min(bus.ticksUntilEvent(), stopTicks - elapsed.ticks);
        bus.tick(amount);
        clock.advanceIdle(amount);
        elapsed.ticks += amount;
        elapsed.idleTicks += amount;
        return amount;
    }

private:
    ARM920T& cpu;
    Bus& bus;
    InterpreterExecution interpreter;
    bool registerClocks;
    size_t ratio;
    size_t phase = 0;
    EmulationClock clock;
    ExecutionTime elapsed;
};

} // namespace oceanblast
