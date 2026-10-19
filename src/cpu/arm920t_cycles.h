#pragma once
#include <cstdint>

namespace oceanblast {
// ARM920T TRM DDI 0151C, chapter 12: cached, no external memory waits.
// This reviewed integer subset is shared by scalar and native execution.
// Load-use interlocks, memory waits and coprocessor costs remain separate work.
struct ARM920TCycles {
    static constexpr uint32_t failedCondition = 1;
    static constexpr uint32_t branch = 3;
    static constexpr uint32_t singleMemory = 1;
    static constexpr uint32_t integer(bool registerShift) { return registerShift ? 2 : 1; }
    static constexpr uint32_t dataProcessing(uint32_t encoding) {
        const uint32_t opcode = (encoding >> 21) & 15;
        const bool registerShift = !(encoding & (1u << 25)) && (encoding & (1u << 4));
        const bool writesPc = ((encoding >> 12) & 15) == 15 && !(opcode >= 8 && opcode <= 11);
        return integer(registerShift) + (writesPc ? 2 : 0);
    }
};
}
