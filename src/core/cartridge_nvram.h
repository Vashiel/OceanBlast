#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace oceanblast {
inline void migrateCartridgeSettings(std::array<uint8_t,2048>& image,uint64_t size,uint32_t crc) {
    // An unsupported difficulty leaves Chefs' player health uninitialized.
    if (size==17301504 && crc==0x93209877u && image[4]>2) image[4]=1;
}
// Separate cartridge-owned bytes while preserving the legacy board image.
inline std::filesystem::path prepareCartridgeNvram(const std::filesystem::path& folder,
                                                  const std::filesystem::path& rom) {
    std::ifstream input(rom, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read the cartridge.");
    static const auto table = [] {
        std::array<uint32_t,256> result{};
        for (uint32_t i=0;i<256;++i) {
            auto value=i;
            for (int bit=0;bit<8;++bit) value=(value>>1)^(0xedb88320u & (0u-(value&1)));
            result[i]=value;
        }
        return result;
    }();
    uint32_t crc=~0u; uint64_t size=0;
    std::array<char,65536> block{};
    while (input) {
        input.read(block.data(),block.size());
        size+=static_cast<uint64_t>(input.gcount());
        for (std::streamsize i=0;i<input.gcount();++i)
            crc=table[(crc^static_cast<uint8_t>(block[i]))&255]^(crc>>8);
    }
    if (!input.eof() || !size) throw std::runtime_error("Cannot read the cartridge.");
    crc^=~0u;
    std::ostringstream key;
    key<<std::hex<<size<<'-'<<std::setw(8)<<std::setfill('0')<<crc<<".nvram";
    const auto directory=folder/"nvram";
    const auto destination=directory/key.str();
    std::filesystem::create_directories(directory);
    if (std::filesystem::exists(destination)) {
        if (std::filesystem::file_size(destination)!=2048)
            throw std::runtime_error("The cartridge save has an invalid size.");
        return destination;
    }
    std::array<uint8_t,2048> image;image.fill(0xff);
    const auto legacy=folder/"board.nvram";
    if (std::filesystem::exists(legacy)) {
        if (std::filesystem::file_size(legacy)!=image.size())
            throw std::runtime_error("The previous device save has an invalid size.");
        std::ifstream source(legacy,std::ios::binary);
        if (!source.read(reinterpret_cast<char*>(image.data()),image.size()))
            throw std::runtime_error("Cannot read the previous device save.");
        migrateCartridgeSettings(image,size,crc);
    }
    auto temporary=destination;temporary+=".tmp";
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(image.data()),image.size()))
            throw std::runtime_error("Cannot write the cartridge save.");
        output.close();
        if (!output) throw std::runtime_error("Cannot finish writing the cartridge save.");
    }
    std::filesystem::rename(temporary,destination);
    return destination;
}
}
