#pragma once
#include <cstddef>
#include <cstdint>
namespace oceanblast {
struct CartridgeSettings { const char* name; size_t cpuStepsPerTick; };
struct CartridgeIdentity { size_t size; uint32_t crc; const char* name; size_t ratio; bool video; };
inline const CartridgeIdentity* identifyCartridge(size_t size, uint32_t crc) {
    // Ratios are historical legacy compatibility settings, not independently
    // measured CPU frequencies or calibrated requirements of these cartridges.
    static constexpr CartridgeIdentity known[] = {
        {17301504, 0x9bce041au, "Crazy Jack", 2, false},
        {69206016, 0x40a18574u, "Cuccioli Cerca Amici", 3, false},
        {17301504, 0xc57e4b7cu, "DigiQUAD", 2, false},
        {34603008, 0xb0b98ad1u, "Gormiti: 2 episodi (Video)", 3, true},
        {69206016, 0x8bf52908u, "Gormiti: Dalle Origini all'Eclissi (Video)", 3, true},
        {34603008, 0xddeec7eau, "Gormiti: Agguato nella Valle", 3, false},
        {34603008, 0xd11135f5u, "Gormiti: Lotta Oscura", 3, false},
        {17301504, 0xf890f74cu, "Gormiti: Masters of the Gorm Island", 3, false},
        {17301504, 0xd1d6118fu, "Pitfall", 3, false},
        {17301504, 0xe3e1953eu, "Rayman 3", 4, false},
        {69206016, 0xc24cb643u, "Sonic X: Volume 1 (Video)", 3, true},
        {17301504, 0x142577eeu, "Spider-Man: Mysterio's Menace", 3, false},
        {34603008, 0x69ab74c0u, "SpongeBob + 5 Atari Games", 3, false},
        {69206016, 0x56deccb3u, "SpongeBob SquarePants: Volume 1 (Video)", 3, true},
        {17301504, 0x93209877u, "Superstar Chefs", 3, false},
        {69206016, 0x2f88baafu, "Totally Spies!: Volume 1 (IT Video)", 3, true},
        {69206016, 0x93e46afbu, "Totally Spies!: Volume 1 (Video)", 3, true},
        {17301504, 0xeba2a2c3u, "Wade Hixton's Counter Punch", 6, false},
        {17301504, 0xb048a789u, "MP3 player 256MB", 3, false},
        {34603008, 0x6b5ca82eu, "Winx Club + 5 Atari Games (IT ES)", 3, false},
        {34603008, 0xeb1d8fdau, "Winx Club + 5 Atari Games", 3, false},
        {34603008, 0x653adfebu, "Winx Club + 5 Atari Games", 3, false},
        {69206016, 0xa870766eu, "Winx Club: Volume 1 (IT ES Video)", 3, true},
        {69206016, 0xee696172u, "Winx Club: Volume 1 (Video)", 3, true},
        {69206016, 0x2494e958u, "Yu-Gi-Oh!: Volume 1 (Video)", 3, true},
    };
    for (const auto& cartridge : known)
        if (cartridge.size == size && cartridge.crc == crc) return &cartridge;
    return nullptr;
}
inline CartridgeSettings resolveCartridgeSettings(size_t size, uint32_t crc,
        bool automatic, bool explicitRatio, size_t ratio, bool registerTiming) {
    if (registerTiming) return {"Register clocks (experimental)", ratio};
    if (explicitRatio) return {"Manual CPU ratio", ratio};
    if (!automatic) return {"Standard", 1};
    if (const auto* cartridge = identifyCartridge(size, crc)) return {cartridge->name, cartridge->ratio};
    return {"Default (unrecognized cartridge)", 1};
}
}
