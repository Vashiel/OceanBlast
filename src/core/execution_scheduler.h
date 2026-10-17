#pragma once
#include "execution_batch.h"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace oceanblast {

struct ExecutionTime {
    uint64_t ticks = 0;
    uint64_t idleTicks = 0;
};

enum class ExecutionExitReason { Budget, Deadline, CpuEvent, ContextChange, Waiting, Halted };
struct ExecutionExit {
    ExecutionExitReason reason;
    size_t steps;
    uint64_t ticks;
    std::optional<ARM920T::StepEvent> event;
};

// Owns time advancement for one machine. Host pacing and presentation remain
// outside this scheduler. Deadlines are observed between guest instructions.
class ExecutionScheduler {
public:
    ExecutionScheduler(ARM920T& cpu, Bus& bus, bool registerClocks, size_t ratio)
        : cpu(&cpu), bus(bus), interpreter(std::in_place,cpu,bus), registerClocks(registerClocks), ratio(ratio) {
        if (!cpu.usesBus(bus)) throw std::invalid_argument("CPU and scheduler bus must match");
        if (!ratio || ratio>16) throw std::invalid_argument("CPU ratio must be between 1 and 16");
    }

    // Only execution views of this same machine may share its timeline.
    // The caller keeps the selected view alive until another view is selected.
    void selectInterpreter(ARM920T& next) {
        if (!cpu->sharesContext(next)) throw std::invalid_argument("Execution state and bus must match");
        if (!cpu->canHandoff() || !next.canHandoff())
            throw std::logic_error("Legacy acceleration must finish before engine handoff");
        next.enterExecution(registerClocks);
        cpu = &next;
        interpreter.emplace(next,bus);
    }

    // Reference boundary for differential execution. Budget counts step calls,
    // including exception/interrupt entry, rather than retired instructions.
    ExecutionExit runToBoundary(size_t budget, uint64_t stopTicks = UINT64_MAX) {
        const uint64_t start = elapsed.ticks;
        uint64_t deadline = elapsed.ticks + std::min(bus.ticksUntilEvent(),stopTicks > elapsed.ticks ? stopTicks-elapsed.ticks : 0);
        size_t steps = 0;
        auto result = [&](ExecutionExitReason reason) {
            return ExecutionExit{reason,steps,elapsed.ticks-start,
                reason == ExecutionExitReason::CpuEvent ? std::optional<ARM920T::StepEvent>(cpu->getLastEvent()) : std::nullopt};
        };
        for (;;) {
            if (cpu->isHalted()) return result(ExecutionExitReason::Halted);
            if (elapsed.ticks >= deadline) return result(ExecutionExitReason::Deadline);
            if (cpu->isWaitingForInterrupt() && !bus.hasPendingIrq()) return result(ExecutionExitReason::Waiting);
            if (steps == budget) return result(ExecutionExitReason::Budget);
            const auto& before=cpu->getState();
            const u32 cpsr=before.cpsr, control=before.cp15_control, ttb=before.cp15_ttb, dacr=before.cp15_dacr;
            step();
            ++steps;
            if (cpu->getLastEvent() != ARM920T::StepEvent::Instruction)
                return result(ExecutionExitReason::CpuEvent);
            const auto& after=cpu->getState();
            if (((cpsr ^ after.cpsr) & 0xffu) || control!=after.cp15_control ||
                ttb!=after.cp15_ttb || dacr!=after.cp15_dacr)
                return result(ExecutionExitReason::ContextChange);
            // An instruction may have armed an earlier peripheral event.
            if (elapsed.ticks < deadline)
                deadline=elapsed.ticks+std::min(bus.ticksUntilEvent(),deadline-elapsed.ticks);
        }
    }

    const ExecutionTime& time() const { return elapsed; }

    void step() {
        if (registerClocks) interpreter->stepClocked(clock, elapsed.ticks);
        else interpreter->stepLegacy(ratio, phase, elapsed.ticks);
    }

    size_t run(size_t budget, uint64_t stopTicks = UINT64_MAX) {
        if (!budget || cpu->isHalted() || elapsed.ticks >= stopTicks) return 0;
        if (registerClocks) {
            const uint64_t deadline = elapsed.ticks +
                std::min(bus.ticksUntilEvent(), stopTicks - elapsed.ticks);
            // A pending interrupt must be allowed to wake a waiting CPU.
            size_t retired = 0;
            if (cpu->isWaitingForInterrupt() && bus.hasPendingIrq()) {
                step();
                retired = 1;
            }
            return retired + interpreter->runClocked(clock, budget - retired, deadline, elapsed.ticks);
        }
        const uint64_t remaining = stopTicks - elapsed.ticks;
        // At most one peripheral tick elapses per instruction. Only form the
        // ratio product when the deadline can be reached within this budget.
        if (remaining <= budget)
            budget = std::min<uint64_t>(budget, legacyStepsUntilTickLimit(remaining, ratio, phase));
        return interpreter->runLegacy(budget, ratio, phase, elapsed.ticks);
    }

    uint64_t advanceIdle(uint64_t stopTicks) {
        if (!registerClocks || cpu->isHalted() || !cpu->isWaitingForInterrupt() ||
            bus.hasPendingIrq() || elapsed.ticks >= stopTicks) return 0;
        const uint64_t amount = std::min(bus.ticksUntilEvent(), stopTicks - elapsed.ticks);
        bus.tick(amount);
        clock.advanceIdle(amount);
        elapsed.ticks += amount;
        elapsed.idleTicks += amount;
        return amount;
    }

private:
    ARM920T* cpu;
    Bus& bus;
    std::optional<InterpreterExecution> interpreter;
    bool registerClocks;
    size_t ratio;
    size_t phase = 0;
    EmulationClock clock;
    ExecutionTime elapsed;
};

} // namespace oceanblast
