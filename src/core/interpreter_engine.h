#pragma once
#include "execution_engine.h"
#include "execution_batch.h"
#include <algorithm>
#include <stdexcept>

namespace oceanblast {
class InterpreterEngine final : public ExecutionEngine {
public:
    InterpreterEngine(ARM920T& cpu,Bus& bus) : cpu(cpu),bus(bus),interpreter(cpu,bus) {
        if (!cpu.usesBus(bus)) throw std::invalid_argument("CPU and engine bus must match");
    }
    const ARM920TState& state() const override { return cpu.getState(); }
    const Bus& memory() const override { return bus; }
    bool canHandoff() const override { return cpu.canHandoff(); }
    void enter(bool registerClocks) override { cpu.enterExecution(registerClocks); }

    // Reference boundary for differential execution. Budget counts step calls,
    // including exception/interrupt entry, rather than retired instructions.
    ExecutionExit runToBoundary(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) override {
        const uint64_t start = timeline.elapsed.ticks;
        uint64_t deadline = timeline.elapsed.ticks + std::min(bus.ticksUntilEvent(),stopTicks > timeline.elapsed.ticks ? stopTicks-timeline.elapsed.ticks : 0);
        size_t steps = 0;
        auto result = [&](ExecutionExitReason reason) {
            return ExecutionExit{reason,steps,timeline.elapsed.ticks-start,
                reason == ExecutionExitReason::CpuEvent ? std::optional<ARM920T::StepEvent>(cpu.getLastEvent()) : std::nullopt};
        };
        for (;;) {
            if (cpu.isHalted()) return result(ExecutionExitReason::Halted);
            if (timeline.elapsed.ticks >= deadline) return result(ExecutionExitReason::Deadline);
            if (cpu.isWaitingForInterrupt() && !bus.hasPendingIrq()) return result(ExecutionExitReason::Waiting);
            if (steps == budget) return result(ExecutionExitReason::Budget);
            const auto& before=cpu.getState();
            const u32 cpsr=before.cpsr, control=before.cp15_control, ttb=before.cp15_ttb, dacr=before.cp15_dacr;
            step(timeline);
            ++steps;
            if (cpu.getLastEvent() != ARM920T::StepEvent::Instruction)
                return result(ExecutionExitReason::CpuEvent);
            const auto& after=cpu.getState();
            if (((cpsr ^ after.cpsr) & 0xffu) || control!=after.cp15_control ||
                ttb!=after.cp15_ttb || dacr!=after.cp15_dacr)
                return result(ExecutionExitReason::ContextChange);
            // An instruction may have armed an earlier peripheral event.
            if (timeline.elapsed.ticks < deadline)
                deadline=timeline.elapsed.ticks+std::min(bus.ticksUntilEvent(),deadline-timeline.elapsed.ticks);
        }
    }

    void step(ExecutionTimeline& timeline) override {
        if (timeline.registerClocks) interpreter.stepClocked(timeline.clock, timeline.elapsed.ticks);
        else interpreter.stepLegacy(timeline.ratio, timeline.phase, timeline.elapsed.ticks);
    }

    size_t run(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) override {
        if (!budget || cpu.isHalted() || timeline.elapsed.ticks >= stopTicks) return 0;
        if (timeline.registerClocks) {
            const uint64_t deadline = timeline.elapsed.ticks +
                std::min(bus.ticksUntilEvent(), stopTicks - timeline.elapsed.ticks);
            // A pending interrupt must be allowed to wake a waiting CPU.
            size_t retired = 0;
            if (cpu.isWaitingForInterrupt() && bus.hasPendingIrq()) {
                step(timeline);
                retired = 1;
            }
            return retired + interpreter.runClocked(timeline.clock, budget - retired, deadline, timeline.elapsed.ticks);
        }
        const uint64_t remaining = stopTicks - timeline.elapsed.ticks;
        // At most one peripheral tick elapses per instruction. Only form the
        // ratio product when the deadline can be reached within this budget.
        if (remaining <= budget)
            budget = std::min<uint64_t>(budget, legacyStepsUntilTickLimit(remaining, timeline.ratio, timeline.phase));
        return interpreter.runLegacy(budget, timeline.ratio, timeline.phase, timeline.elapsed.ticks);
    }

private:
    ARM920T& cpu;
    Bus& bus;
    InterpreterExecution interpreter;
};
} // namespace oceanblast
