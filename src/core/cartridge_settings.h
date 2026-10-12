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
    if (size == 34603008 && crc == 0xb0b98ad1u) return {"Gormiti: 2 episodi (Video)", 3};
    if (size == 69206016 && crc == 0x8bf52908u) return {"Gormiti: Dalle Origini all'Eclissi (Video)", 3};
    if (size == 34603008 && crc == 0xddeec7eau) return {"Gormiti: Agguato nella Valle", 3};
    if (size == 34603008 && crc == 0xd11135f5u) return {"Gormiti: Lotta Oscura", 3};
    if (size == 17301504 && crc == 0xf890f74cu) return {"Gormiti: Masters of the Gorm Island", 3};
    if (size == 17301504 && crc == 0xd1d6118fu) return {"Pitfall", 3};
    if (size == 17301504 && crc == 0xe3e1953eu) return {"Rayman 3", 4};
    if (size == 69206016 && crc == 0xc24cb643u) return {"Sonic X: Volume 1 (Video)", 3};
    if (size == 17301504 && crc == 0x142577eeu) return {"Spider-Man: Mysterio's Menace", 3};
    if (size == 34603008 && crc == 0x69ab74c0u) return {"SpongeBob + 5 Atari Games", 3};
    if (size == 69206016 && crc == 0x56deccb3u) return {"SpongeBob SquarePants: Volume 1 (Video)", 3};
    if (size == 17301504 && crc == 0x93209877u) return {"Superstar Chefs", 3};
    if (size == 69206016 && crc == 0x2f88baafu) return {"Totally Spies!: Volume 1 (IT Video)", 3};
    if (size == 69206016 && crc == 0x93e46afbu) return {"Totally Spies!: Volume 1 (Video)", 3};
    if (size == 17301504 && crc == 0xeba2a2c3u) return {"Wade Hixton's Counter Punch", 3};
    if (size == 34603008 && crc == 0x6b5ca82eu) return {"Winx Club + 5 Atari Games (IT ES)", 3};
    if (size == 34603008 && (crc == 0xeb1d8fdau || crc == 0x653adfebu)) return {"Winx Club + 5 Atari Games", 3};
    if (size == 69206016 && crc == 0xa870766eu) return {"Winx Club: Volume 1 (IT ES Video)", 3};
    if (size == 69206016 && crc == 0xee696172u) return {"Winx Club: Volume 1 (Video)", 3};
    if (size == 69206016 && crc == 0x2494e958u) return {"Yu-Gi-Oh!: Volume 1 (Video)", 3};
    return {"Default (unrecognized cartridge)", 1};
}
}
