#include "core/cartridge_settings.h"
#include "core/cartridge_language.h"
#include <iostream>
int main() {
    using oceanblast::resolveCartridgeSettings;
    using oceanblast::resolveCartridgeLanguageByte;
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; };
    check("Pitfall automatic ratio", resolveCartridgeSettings(17301504,0xd1d6118f,true,false,1,false).cpuStepsPerTick == 3);
    check("Rayman 3 automatic ratio", resolveCartridgeSettings(17301504,0xe3e1953e,true,false,1,false).cpuStepsPerTick == 4);
    check("Chefs automatic ratio", resolveCartridgeSettings(17301504,0x93209877,true,false,1,false).cpuStepsPerTick == 3);
    check("Wade automatic ratio", resolveCartridgeSettings(17301504,0xeba2a2c3,true,false,1,false).cpuStepsPerTick == 3);
    check("MP3 firmware automatic ratio", resolveCartridgeSettings(17301504,0xb048a789,true,false,1,false).cpuStepsPerTick == 3);
    check("Localized video identities enable complete sweeps", oceanblast::identifyCartridge(69206016,0x2f88baaf)->video && oceanblast::identifyCartridge(69206016,0xa870766e)->video);
    check("Crazy Jack automatic ratio", resolveCartridgeSettings(17301504,0x9bce041a,true,false,1,false).cpuStepsPerTick == 2);
    check("Cuccioli automatic ratio", resolveCartridgeSettings(69206016,0x40a18574,true,false,1,false).cpuStepsPerTick == 3);
    check("Gormiti 2 episodi video ratio", resolveCartridgeSettings(34603008,0xb0b98ad1,true,false,1,false).cpuStepsPerTick == 3);
    check("Sonic X video ratio", resolveCartridgeSettings(69206016,0xc24cb643,true,false,1,false).cpuStepsPerTick == 3);
    check("Winx Club video ratio", resolveCartridgeSettings(69206016,0xee696172,true,false,1,false).cpuStepsPerTick == 3);
    check("Winx + 5 Atari Games ratio", resolveCartridgeSettings(34603008,0xeb1d8fda,true,false,1,false).cpuStepsPerTick == 3);
    check("Winx + 5 Atari Games repaired ratio", resolveCartridgeSettings(34603008,0x653adfeb,true,false,1,false).cpuStepsPerTick == 3);
    check("Exact video identity is independent of manual timing", oceanblast::identifyCartridge(69206016,0xc24cb643)->video && resolveCartridgeSettings(69206016,0xc24cb643,true,true,4,false).cpuStepsPerTick == 4);
    check("Games and combined cartridges retain unlatched native display", !oceanblast::identifyCartridge(17301504,0xe3e1953e)->video && !oceanblast::identifyCartridge(34603008,0x69ab74c0)->video);
    check("Unknown or wrong-sized images have no video identity", !oceanblast::identifyCartridge(69206015,0xc24cb643) && !oceanblast::identifyCartridge(69206016,0));
    check("Unknown checksum fallback", resolveCartridgeSettings(17301504,0,true,false,1,false).cpuStepsPerTick == 1);
    check("Wrong size fallback", resolveCartridgeSettings(17301503,0xd1d6118f,true,false,1,false).cpuStepsPerTick == 1);
    check("Wade wrong size fallback", resolveCartridgeSettings(17301503,0xeba2a2c3,true,false,1,false).cpuStepsPerTick == 1);
    check("Explicit 1x overrides automatic", resolveCartridgeSettings(17301504,0xd1d6118f,true,true,1,false).cpuStepsPerTick == 1);
    check("Explicit 4x overrides automatic", resolveCartridgeSettings(17301504,0xd1d6118f,true,true,4,false).cpuStepsPerTick == 4);
    check("Disabled profiles", resolveCartridgeSettings(17301504,0xd1d6118f,false,false,1,false).cpuStepsPerTick == 1);
    check("Register timing overrides profile", resolveCartridgeSettings(17301504,0xd1d6118f,true,false,1,true).cpuStepsPerTick == 1);
    check("Explicit cartridge language overrides UI and filename", resolveCartridgeLanguageByte("de", 0, "Title (EN).bin") == 3);
    check("Keep preserves existing EEPROM", resolveCartridgeLanguageByte("keep", 0, "Title.bin") == -1);
    check("Ask restores guest language menu", resolveCartridgeLanguageByte("ask", 0, "Title.bin") == 0xff);
    check("Auto selects supported UI language", resolveCartridgeLanguageByte("auto", 2, "Title (IT ES).bin") == 9);
    check("Auto asks when explicit language list excludes UI", resolveCartridgeLanguageByte("auto", 0, "Title (IT ES).bin") == 0xff);
    check("M10 does not imply unsupported language", resolveCartridgeLanguageByte("auto", 0, "Rayman 3 (M10).bin") == 1);
    check("Unknown filename tags do not override UI", resolveCartridgeLanguageByte("auto", 0, "Title (revision).bin") == 1);
    check("All UI languages map to guest EEPROM", resolveCartridgeLanguageByte("auto", 4, "Title (EU).bin") == 16 && resolveCartridgeLanguageByte("auto", 6, "Title.bin") == 15);
    return failures ? 1 : 0;
}
