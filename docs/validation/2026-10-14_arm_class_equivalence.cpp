#include <array>
#include <cstdint>
#include <iostream>
using u8 = uint8_t; using u32 = uint32_t;

// Classify only bits represented in the key. Full-word checks below retain
// control-register, swap and coprocessor distinctions within these classes.
static constexpr std::array<u8, 4096> makeArmDecodeClasses() {
    std::array<u8, 4096> result{};
    for (u32 key = 0; key < result.size(); ++key) {
        const u32 instr = ((key & 0xff0u) << 16) | ((key & 15u) << 4);
        u8 kind = (instr >> 25) & 7;
        if (kind == 0 && (instr & 0x90) == 0x90) {
            if (instr & 0x60) kind = 8; // Halfword transfer family.
            else if (!(instr & (1u << 24))) kind = 9; // Multiply family.
            else kind = 10; // Swap or general data-processing fallback.
        } else if (kind == 7 && (instr & (1u << 24))) {
            kind = 11; // Software interrupt.
        }
        result[key] = kind;
    }
    return result;
}
static constexpr auto armDecodeClasses = makeArmDecodeClasses();

static u8 referenceClass(u32 instr) {
    const u8 major = (instr >> 25) & 7;
    if (major == 0) {
        if ((instr & 0x90) != 0x90) return 0;
        if (instr & 0x60) return 8;
        if (!(instr & (1u << 24))) return 9;
        return 10;
    }
    if (major == 7 && (instr & 0x0f000000) == 0x0f000000) return 11;
    return major;
}
int main() {
    u32 random = 0x72941abc;
    for (u32 key = 0; key < 4096; ++key) for (u32 trial = 0; trial < 256; ++trial) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const u32 instr = (random & ~0x0ff000f0u) | ((key & 0xff0u) << 16) | ((key & 15u) << 4);
        if (armDecodeClasses[key] != referenceClass(instr)) return 1;
    }
    std::cout << "PASS 1048576 class comparisons across all keys and omitted bits\n";
}
