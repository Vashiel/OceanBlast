#pragma once
#include <array>
#include <cstdint>

namespace oceanblast {
class ButtonPulses {
public:
    uint32_t mask() const { return active; }
    void press(uint32_t buttons, uint64_t now, uint32_t duration) {
        buttons &= 0x7fff;
        for (unsigned bit = 0; bit < deadlines.size(); ++bit)
            if (buttons & (1u << bit)) deadlines[bit] = now + (duration ? duration : 180u);
        active |= buttons;
    }
    void release(uint32_t buttons) { active &= ~buttons; }
    void clear() { active = 0; deadlines.fill(0); }
    void expire(uint64_t now) {
        for (unsigned bit = 0; bit < deadlines.size(); ++bit)
            if ((active & (1u << bit)) && now >= deadlines[bit]) active &= ~(1u << bit);
    }
private:
    uint32_t active = 0;
    std::array<uint64_t, 15> deadlines{};
};
}
