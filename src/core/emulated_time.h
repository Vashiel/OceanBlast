#pragma once
#include <cstdint>

namespace oceanblast {
// Internal units shared by CPU time conversion and clocked devices. This is
// neither the board crystal nor a claim about instructions executed per second.
struct EmulatedTime {
    static constexpr uint64_t ticksPerSecond=20000000;
    static constexpr double seconds(uint64_t ticks) { return double(ticks)/ticksPerSecond; }
};
}
