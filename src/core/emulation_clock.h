#pragma once
#include <cstdint>

namespace oceanblast {
// Peripheral models use a shared 20 MHz time unit. CPU cycles are converted
// using the programmed FCLK, preserving fractional time across clock changes.
class EmulationClock {
public:
    static constexpr uint64_t ticksPerSecond = 20000000;
    uint64_t advance(uint32_t cycles, uint32_t hz) {
        if (!hz || !cycles) return 0;
        if (hz != frequency) {
            frequency = hz;
            ticksPerCycle = ((ticksPerSecond << 32) + hz - 1) / hz;
        }
        fraction += uint64_t(cycles) * ticksPerCycle;
        const uint64_t whole = fraction >> 32;
        fraction &= 0xffffffffull;
        ticks += whole;
        return whole;
    }
    void advanceIdle(uint64_t amount) { ticks += amount; }
    uint64_t elapsedTicks() const { return ticks; }
    double seconds() const { return double(ticks) / ticksPerSecond; }
private:
    uint32_t frequency = 0;
    uint64_t ticksPerCycle = 0, fraction = 0, ticks = 0;
};
}
