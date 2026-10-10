#pragma once
#include "memory/bus.h"
#include <cstring>
#include <set>

namespace oceanblast {
// Diagnostic inspection only: no guest accesses, faults, or TLB changes.
// Accept a direct-mapped kernel export only after both names agree on its value.
inline u32 findKernelJiffies(const Bus& bus) {
    const auto* data = bus.getSdramPtr();
    constexpr size_t length = 2 * 1024 * 1024;
    u32 names[2]{};
    for (size_t i = 0; i + 11 <= length; ++i) {
        if(i && data[i-1]) continue;
        if (!std::memcmp(data+i,"jiffies\0",8)) names[0] = 0xc0000000u + u32(i);
        if (!std::memcmp(data+i,"jiffies_64\0",11)) names[1] = 0xc0000000u + u32(i);
    }
    if (!names[0] || !names[1]) return 0;
    std::set<u32> addresses[2];
    for (size_t i = 4; i + 4 <= length; i += 4) {
        u32 name, value;
        std::memcpy(&name,data+i,4); std::memcpy(&value,data+i-4,4);
        if (value < 0xc0000000u || value >= 0xc0000000u+length || (value&3)) continue;
        for (unsigned n=0;n<2;++n) if(name==names[n]) addresses[n].insert(value);
    }
    u32 found=0;
    for(u32 candidate:addresses[0]) if(addresses[1].count(candidate)) {
        if(found) return 0;
        found=candidate;
    }
    return found;
}

inline bool readKernelJiffies(const Bus& bus, u32 address, u32& value) {
    if (!address || !bus.isMmuEnabled()) return false;
    const uint64_t table = uint64_t(bus.getTtb() & 0xffffc000u) + (address >> 20) * 4;
    if (table < ADDR_SDRAM_BASE || table+4 > uint64_t(ADDR_SDRAM_BASE)+ADDR_SDRAM_SIZE) return false;
    u32 descriptor;
    std::memcpy(&descriptor,bus.getSdramPtr()+table-ADDR_SDRAM_BASE,4);
    if ((descriptor&3)!=2) return false; // Only the observed section mapping is accepted.
    const u32 physical = (descriptor & 0xfff00000u) | (address & 0xfffffu);
    if (physical < ADDR_SDRAM_BASE || uint64_t(physical)+4 > uint64_t(ADDR_SDRAM_BASE)+ADDR_SDRAM_SIZE) return false;
    if (physical-ADDR_SDRAM_BASE != address-0xc0000000u) return false;
    std::memcpy(&value,bus.getSdramPtr()+physical-ADDR_SDRAM_BASE,4);
    return true;
}
}
