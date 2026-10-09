#pragma once
#include "../memory/bus.h"
#include <cstring>

namespace oceanblast {
// Read defined function exports from mapped guest ELF objects without guest execution.
inline u32 findGuestExport(Bus& bus, const char* wanted) {
    auto word=[&](u32 address,u32& value){return bus.peek32(address,value);};
    for(u32 address=0x40000000;address<0x41000000;address+=4096){
        u32 magic=0,offset=0,layout=0,count=0;
        if(!word(address,magic)||magic!=0x464c457f||!word(address+28,offset)||
           !word(address+40,layout)||!word(address+44,count))continue;
        const u32 phSize=layout>>16,phCount=count&65535;
        if(phSize<32||phCount>64||!phCount||offset>65536)continue;
        u32 minimum=~0u,dynamic=0,dynamicSize=0;
        for(u32 i=0;i<phCount;++i){
            u32 type=0,va=0,size=0;
            if(!word(address+offset+i*phSize,type)||!word(address+offset+i*phSize+8,va)||!word(address+offset+i*phSize+16,size))continue;
            if(type==1)minimum=std::min(minimum,va&~4095u);
            if(type==2){dynamic=va;dynamicSize=size;}
        }
        if(minimum==~0u||!dynamic||dynamicSize>32768)continue;
        const u32 base=address-minimum;u32 strings=0,symbols=0,hash=0;
        for(u32 i=0;i<dynamicSize;i+=8){
            u32 tag=0,value=0;if(!word(base+dynamic+i,tag)||!word(base+dynamic+i+4,value)||!tag)break;
            const u32 pointer=value<base?base+value:value;
            if(tag==4)hash=pointer;else if(tag==5)strings=pointer;else if(tag==6)symbols=pointer;
        }
        u32 names=0;if(!strings||!symbols||!hash||!word(hash+4,names)||names>100000)continue;
        for(u32 i=0;i<names;++i){
            u32 name=0,value=0,info=0;
            if(!word(symbols+16*i,name)||!word(symbols+16*i+4,value)||!word(symbols+16*i+12,info)||!name||!(info>>16)||(info&15)!=2)continue;
            const size_t length = std::strlen(wanted) + 1; bool match=true;
            for(size_t byte=0;byte<length;++byte){u8 actual=0;if(!bus.peek8(strings+name+u32(byte),actual)||actual!=wanted[byte]){match=false;break;}}
            if(match)return base+value;
        }
    }
    return 0;
}
}
