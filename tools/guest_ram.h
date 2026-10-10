#pragma once
#include "memory/bus.h"
#include <cstring>

namespace oceanblast {
// Read mapped SDRAM only. Never touch MMIO, privileges, TLB entries or faults.
// Translation is structural inspection, not a guest access-permission check.
inline bool readGuestRam32(const Bus& bus, u32 address, u32& value) {
    if(address&3)return false;
    auto physicalWord=[&](uint64_t physical,u32& word) {
        if(physical<ADDR_SDRAM_BASE||physical+4>uint64_t(ADDR_SDRAM_BASE)+ADDR_SDRAM_SIZE)return false;
        std::memcpy(&word,bus.getSdramPtr()+physical-ADDR_SDRAM_BASE,4);
        return true;
    };
    u32 physical=address;
    if(bus.isMmuEnabled()) {
        u32 descriptor;
        if(!physicalWord(uint64_t(bus.getTtb()&0xffffc000u)+(address>>20)*4,descriptor))return false;
        const u32 kind=descriptor&3;
        if(kind==2)physical=(descriptor&0xfff00000u)|(address&0xfffffu);
        else if(kind==1||kind==3) {
            const uint64_t table=kind==1?(descriptor&0xfffffc00u):(descriptor&0xfffff000u);
            const u32 index=kind==1?((address>>12)&255):((address>>10)&1023);
            u32 page;
            if(!physicalWord(table+index*4,page))return false;
            if((page&3)==1)physical=(page&0xffff0000u)|(address&0xffffu);
            else if((page&3)==2||(kind==1&&(page&3)==3))physical=(page&0xfffff000u)|(address&4095);
            else if(kind==3&&(page&3)==3)physical=(page&0xfffffc00u)|(address&1023);
            else return false;
        } else return false;
    }
    return physicalWord(physical,value);
}
}
