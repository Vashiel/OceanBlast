#pragma once
#include <cstdint>

namespace oceanblast {
// Timer input follows PCLK; caller ticks retain the nominal 20M/s time base.
class Timer4 {
public:
    void reset() { *this = Timer4(); }
    void setClock(uint32_t hz) { pclk = hz; }
    void configure(uint32_t cfg0, uint32_t cfg1) {
        const uint32_t count = observe();
        const uint32_t mux = (cfg1 >> 16) & 15;
        external = mux >= 4; // No external TCLK1 source is currently modeled.
        denominator = 20000000ull * (((cfg0 >> 8) & 255) + 1) * (2ull << (mux & 3));
        if (remaining) remaining = (uint64_t(count) + 1) * denominator;
    }
    void setBuffer(uint32_t value) { buffer = value & 0xffff; }
    void control(uint32_t value) {
        if (value & (1u << 21)) remaining = (uint64_t(buffer) + 1) * denominator;
        running = (value & (1u << 20)) != 0;
        reload = (value & (1u << 22)) != 0;
    }
    uint32_t observe() const { return remaining ? uint32_t((remaining - 1) / denominator) : 0; }
    bool isRunning() const { return running && !external && remaining != 0; }
    uint64_t ticksUntilExpiry() const {
        return isRunning() ? (remaining + pclk - 1) / pclk : UINT64_MAX;
    }
    bool advance(uint64_t instructions) {
        if (!isRunning()) return false;
        const uint64_t elapsed = instructions * pclk;
        if (elapsed < remaining) { remaining -= elapsed; return false; }
        if (reload) {
            const uint64_t period = (uint64_t(buffer) + 1) * denominator;
            remaining = period - (elapsed - remaining) % period;
        } else { remaining = 0; running = false; }
        return true;
    }
private:
    uint64_t denominator = 40000000, remaining = 0;
    uint32_t buffer = 0;
    uint32_t pclk = 12000000;
    bool running = false, reload = false, external = false;
};
}
