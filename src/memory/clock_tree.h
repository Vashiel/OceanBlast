#pragma once
#include <cstdint>

namespace oceanblast {
// S3C2410A clock registers, Samsung manual section 7. The board input is 12 MHz.
class ClockTree {
public:
    static constexpr uint32_t crystal = 12000000;
    void reset() { *this = ClockTree(); }
    void setMpll(uint32_t value) { mpll = value; pllSelected = true; }
    void setSlow(uint32_t value) { slow = value; }
    void setDivider(uint32_t value) { divider = value; }
    uint32_t fclk() const {
        if (!pllSelected || (slow & (1u << 4))) {
            const uint32_t factor = (slow & (1u << 4)) ? 2 * (slow & 7) : 1;
            return crystal / (factor ? factor : 1);
        }
        const uint64_t numerator = uint64_t(crystal) * (((mpll >> 12) & 255) + 8);
        return uint32_t(numerator / ((((mpll >> 4) & 63) + 2) * (1u << (mpll & 3))));
    }
    uint32_t hclk() const { return fclk() / ((divider & 4) ? 4 : ((divider & 2) ? 2 : 1)); }
    uint32_t pclk() const { return hclk() / ((divider & 4) ? 1 : ((divider & 1) ? 2 : 1)); }
private:
    uint32_t mpll = 0x5c080, slow = 4, divider = 0;
    bool pllSelected = false; // Reset uses the input clock until MPLLCON is written.
};
}
