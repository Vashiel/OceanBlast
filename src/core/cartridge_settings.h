#pragma once
#include <cstddef>
#include <cstdint>
namespace oceanblast {
struct CartridgeSettings { const char* name; size_t cpuStepsPerTick; };
inline CartridgeSettings resolveCartridgeSettings(size_t size, uint32_t crc,
        bool automatic, bool explicitRatio, size_t ratio, bool registerTiming) {
    if (registerTiming) return {"Register clocks (experimental)", ratio};
    if (explicitRatio) return {"Manual CPU ratio", ratio};
    if (!automatic) return {"Standard", 1};
    if (size == 17301504 && crc == 0xd1d6118fu) return {"Pitfall", 2};
    if (size == 17301504 && crc == 0x93209877u) return {"Superstar Chefs", 2};
    return {"Default (unrecognized cartridge)", 1};
}
}
