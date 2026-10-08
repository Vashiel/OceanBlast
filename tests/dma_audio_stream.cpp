#include "memory/bus.h"
#include <iostream>
#include <vector>
using namespace oceanblast;
int main() {
    int failures=0;
    auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    const uint64_t period=uint64_t(1024)*20000000/22050/4;
    auto setup=[](Bus& b,u32 destination=0x55000010,u32 sourceControl=0){
        b.write32(0x55000008,0xe7);b.write32(0x55000004,0x99);
        b.write32(0x4b000080,0x30010000);b.write32(0x4b000084,sourceControl);
        b.write32(0x4b000088,destination);b.write32(0x4b00008c,1);
        b.write32(0x4b000090,512|(1u<<20)|(1u<<22));b.write32(0x4b0000a0,2);
    };
    { Bus b; b.reset();std::vector<int16_t> heard;
      b.setAudioCallback([&](const int16_t* p,size_t n){heard.insert(heard.end(),p,p+n);});
      for(u32 i=0;i<512;++i)b.write16(0x30010000+i*2,100);
      setup(b);b.tick((period+1)/2);
      check("DMA emits consumed PCM before transfer completion",heard.size()==256);
      for(u32 i=0;i<512;++i)b.write16(0x30010000+i*2,200);
      b.tick(period-(period+1)/2);
      check("DMA preserves samples consumed before RAM overwrite",heard.size()==512 && heard.front()==100 && heard[255]==100 && heard[256]==200 && heard.back()==200);
      check("DMA completion emits exactly one full transfer",heard.size()==512 && b.read32(0x4b000094)==0);
    }
    { Bus b;b.reset();size_t samples=0;b.setAudioCallback([&](const int16_t*,size_t n){samples+=n;});
      setup(b,0x30020000);b.tick(period);
      check("Non-IIS DMA destination does not emit audio",samples==0);
    }
    { Bus b;b.reset();std::vector<int16_t> heard;b.setAudioCallback([&](const int16_t* p,size_t n){heard.insert(heard.end(),p,p+n);});
      b.write16(0x30010000,1234);setup(b,0x55000010,1);b.tick(period);
      check("Fixed-source DMA repeats the transfer item",heard.size()==512 && heard.front()==1234 && heard.back()==1234);
    }
    { Bus b;b.reset();setup(b);b.tick(period);
      check("Masked DMA completion retains source pending",(b.getMmio(0x4a000000)&(1u<<19)) && !b.hasPendingIrq());
      b.write32(0x4a000008,~(1u<<19));
      check("Unmasking DMA delivers the already pending interrupt",b.getMmio(0x4a000010)==(1u<<19));
    }
    { Bus b;b.reset();b.write32(0x4a000008,~3u);b.setButtonMask(1u<<5);b.setButtonMask((1u<<5)|(1u<<4));
      b.write32(0x4a000010,1u<<7);
      check("Unrelated INTPND acknowledgement preserves selected source",b.getMmio(0x4a000010)==2);
      b.write32(0x4a000000,2);b.write32(0x4a000010,2);
      check("Acknowledgement selects the next pending source",b.getMmio(0x4a000010)==1);
    }
    return failures?1:0;
}
