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
    if (size == 17301504 && crc == 0x9bce041au) return {"Crazy Jack", 2};
    if (size == 69206016 && crc == 0x40a18574u) return {"Cuccioli Cerca Amici", 3};
    if (size == 17301504 && crc == 0xc57e4b7cu) return {"DigiQUAD", 2};
    if (size == 34603008 && crc == 0xddeec7eau) return {"Gormiti: Agguato nella Valle", 3};
    if (size == 34603008 && crc == 0xd11135f5u) return {"Gormiti: Lotta Oscura", 3};
    if (size == 17301504 && crc == 0xf890f74cu) return {"Gormiti: Masters of the Gorm Island", 3};
    if (size == 17301504 && crc == 0xd1d6118fu) return {"Pitfall", 3};
    if (size == 17301504 && crc == 0xe3e1953eu) return {"Rayman 3", 3};
    if (size == 17301504 && crc == 0x142577eeu) return {"Spider-Man: Mysterio's Menace", 3};
    if (size == 17301504 && crc == 0x93209877u) return {"Superstar Chefs", 3};
    if (size == 17301504 && crc == 0xeba2a2c3u) return {"Wade Hixton's Counter Punch", 3};
    return {"Default (unrecognized cartridge)", 1};
}
}
