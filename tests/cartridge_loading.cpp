#include "memory/bus.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    // Synthetic damaged-page pattern: no cartridge or proprietary code is used.
    std::vector<oceanblast::u8> source(34603008, 0xff);
    source[60308 * 528] = 0x6d; source[60308 * 528 + 1] = 0xe0;
    source[60310 * 528] = 0x7b; source[60310 * 528 + 1] = 0xde;
    const std::filesystem::path path = "build/test-cartridge-loading.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(source.data()), source.size());
        if (!file) return 1;
    }
    oceanblast::Bus bus;
    const bool identical = bus.loadCartridge(path.string()) && bus.getCartNand() == source;
    std::filesystem::remove(path);
    std::cout << (identical ? "PASS" : "FAIL") << " Cartridge loading preserves every input NAND byte\n";
    return identical ? 0 : 1;
}
