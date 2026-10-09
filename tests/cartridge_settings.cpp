#include "core/cartridge_settings.h"
#include <iostream>
int main() {
    using oceanblast::resolveCartridgeSettings;
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; };
    check("Pitfall automatic ratio", resolveCartridgeSettings(17301504,0xd1d6118f,true,false,1,false).cpuStepsPerTick == 2);
    check("Chefs automatic ratio", resolveCartridgeSettings(17301504,0x93209877,true,false,1,false).cpuStepsPerTick == 2);
    check("Unknown checksum fallback", resolveCartridgeSettings(17301504,0,true,false,1,false).cpuStepsPerTick == 1);
    check("Wrong size fallback", resolveCartridgeSettings(17301503,0xd1d6118f,true,false,1,false).cpuStepsPerTick == 1);
    check("Explicit 1x overrides automatic", resolveCartridgeSettings(17301504,0xd1d6118f,true,true,1,false).cpuStepsPerTick == 1);
    check("Explicit 4x overrides automatic", resolveCartridgeSettings(17301504,0xd1d6118f,true,true,4,false).cpuStepsPerTick == 4);
    check("Disabled profiles", resolveCartridgeSettings(17301504,0xd1d6118f,false,false,1,false).cpuStepsPerTick == 1);
    check("Register timing overrides profile", resolveCartridgeSettings(17301504,0xd1d6118f,true,false,1,true).cpuStepsPerTick == 1);
    return failures ? 1 : 0;
}
