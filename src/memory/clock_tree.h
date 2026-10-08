#pragma once
#include <cstdint>

namespace oceanblast {
// S3C2410A clock registers, Samsung manual section 7. The board input is 12 MHz.
class ClockTree {
public:
    static constexpr uint32_t crystal = 12000000;
    void reset() { *this = ClockTree(); }
    void setMpll(uint32_t value) { mpll = value; pllSelected = true; refresh(); }
    void setSlow(uint32_t value) { slow = value; refresh(); }
    void setDivider(uint32_t value) { divider = value; refresh(); }
    uint32_t fclk() const { return cpuClock; }
    uint32_t hclk() const { return busClock; }
    uint32_t pclk() const { return peripheralClock; }
private:
    uint32_t calculateFclk() const {
        if (!pllSelected || (slow & (1u << 4))) {
            const uint32_t factor = (slow & (1u << 4)) ? 2 * (slow & 7) : 1;
            return crystal / (factor ? factor : 1);
        }
        const uint64_t numerator = uint64_t(crystal) * (((mpll >> 12) & 255) + 8);
        return uint32_t(numerator / ((((mpll >> 4) & 63) + 2) * (1u << (mpll & 3))));
    }
    void refresh() {
        cpuClock = calculateFclk();
        busClock = cpuClock / ((divider & 4) ? 4 : ((divider & 2) ? 2 : 1));
        peripheralClock = busClock / ((divider & 4) ? 1 : ((divider & 1) ? 2 : 1));
    }
    uint32_t cpuClock = crystal, busClock = crystal, peripheralClock = crystal;
    uint32_t mpll = 0x5c080, slow = 4, divider = 0;
    bool pllSelected = false; // Reset uses the input clock until MPLLCON is written.
};
}
