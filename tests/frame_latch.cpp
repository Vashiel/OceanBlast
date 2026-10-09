#include "display/frame_latch.h"
#include "memory/bus.h"
#include <iostream>
#include <vector>
#include <cstring>
using namespace oceanblast;
int main() {
 int failures=0;auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
 FrameLatch frame;std::vector<uint8_t> memory(240*480,0x11);frame.configure(0x302a0000,memory.size());
 frame.store(100,2,memory.data());check("Joining a partial sweep waits for its origin",frame.covered()==0&&!frame.ready());
 for(size_t i=0;i<memory.size();i+=2){memory[i]=memory[i+1]=0x22;frame.store(i,2,memory.data());if(i==memory.size()/2)check("Half-written video remains unpublished",!frame.ready());}
 check("Full sweep publishes exactly one complete frame",frame.completed()==1&&frame.ready()&&std::memcmp(frame.data(),memory.data(),memory.size())==0);
 for(size_t i=0;i<memory.size()/2;i+=2){memory[i]=memory[i+1]=0x33;frame.store(i,2,memory.data());}
 check("Published frame stays immutable during next sweep",frame.completed()==1&&frame.data()[0]==0x22&&frame.data()[memory.size()-1]==0x22);
 frame.store(0,2,memory.data());check("Restarted sweep discards previous partial coverage",frame.covered()==2&&frame.abandoned()==1);
 for(size_t i=2;i<memory.size();i+=2){memory[i]=memory[i+1]=0x44;frame.store(i,2,memory.data());}
 check("Replacement sweep completes without inherited coverage",frame.completed()==2&&frame.data()[memory.size()-1]==0x44);
 frame.configure(0x302a0000,128);std::vector<uint8_t> small(128,0x55);
 frame.store(0,1,small.data());frame.store(0,1,small.data());
 check("Duplicate stores do not inflate coverage",frame.covered()==1);
 for(size_t i=1;i<128;++i)frame.store(i,1,small.data());
 check("Byte stores cover all scanout bytes",frame.ready()&&frame.size()==128);
 frame.configure(0x302a0000,128); // same layout retains published frame
 check("Unchanged configuration retains last frame",frame.ready());
 frame.configure(0x302b0000,128);check("Address transition drops stale capture",!frame.ready()&&frame.covered()==0);
 frame.store(0,63,small.data());frame.store(63,4,small.data());frame.store(67,61,small.data());check("Stores spanning coverage words remain exact",frame.ready());
 Bus bus;bus.enableHostFrameCapture(true);bus.write32(0x4d000000,0x879);bus.write32(0x4d000004,0x303bc605);bus.write32(0x4d000014,0x18150000);bus.write32(0x4d00001c,240);
 check("Programmed video surface enables host capture",bus.hostFrameCaptureActive()&&bus.getHostFrameCapture().size()==115200);
 for(u32 i=0;i<115200;i+=4)bus.write32(0x302a0000+i,0x12345678);
 check("Aligned guest word stores publish after the final store",bus.getHostFrameCapture().completed()==1&&bus.getHostFrameCapture().data()[115199]==0x12);
 bus.write32(0x4d000004,0x4f27d245);check("160-row game surfaces bypass sweep capture",!bus.hostFrameCaptureActive());
 bus.write32(0x4d000004,0x303bc605);for(u32 i=0;i<115200;++i)bus.write8Phys(0x302a0000+i,0x66);
 check("Physical byte writes also complete a sweep",bus.getHostFrameCapture().ready()&&bus.getHostFrameCapture().data()[115199]==0x66);
 bus.write16(0x302a0000,0x7777);check("New guest halfword sweep preserves committed bytes",bus.getHostFrameCapture().data()[0]==0x66);
 bus.write32(0x4d000004,0x4f27d245);bus.enableHostFrameCapture(true,true);check("Recognized native-height video enables capture",bus.hostFrameCaptureActive()&&bus.getHostFrameCapture().size()==76800);
 bus.enableHostFrameCapture(false);check("Raw diagnostic mode disables capture",!bus.hostFrameCaptureActive());
 return failures?1:0;
}
