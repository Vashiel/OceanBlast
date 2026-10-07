#include "cart_parser.h"
#include <iostream>
#include <iomanip>
#include <cstring>

namespace oceanblast {

CartridgeInfo CartParser::parse(const std::vector<u8>& data) {
    CartridgeInfo info;
    info.size = data.size();
    if (data.size() < 512) return info;

    // Check for ARM branch instruction at 0x0 (Standard bootloader)
    u32 firstWord = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    if ((firstWord & 0x0E000000) == 0x0A000000) {
        info.hasBootloader = true;
    }

    // Check for Linux kernel signature ('vmlinux' / 'Linux' / gzip / cramfs / squashfs)
    std::string content(reinterpret_cast<const char*>(data.data()), std::min(data.size(), static_cast<size_t>(1024 * 1024)));
    if (content.find("Linux") != std::string::npos || content.find("vmlinux") != std::string::npos) {
        info.hasLinuxKernel = true;
    }

    info.name = "Nikko digiBLAST Cartridge";
    return info;
}

void CartParser::printInfo(const CartridgeInfo& info) {
    std::cout << "--- [digiBLAST Cartridge Information] ---" << std::endl;
    std::cout << "Cartridge:     " << info.name << std::endl;
    std::cout << "ROM Size:      " << info.size << " bytes (" << (info.size / (1024.0 * 1024.0)) << " MB)" << std::endl;
    std::cout << "Bootloader:    " << (info.hasBootloader ? "Present (ARM9 branch at 0x0)" : "None") << std::endl;
    std::cout << "Linux Kernel:  " << (info.hasLinuxKernel ? "Detected" : "Raw binary") << std::endl;
    std::cout << "-----------------------------------------" << std::endl;
}

} // namespace oceanblast
