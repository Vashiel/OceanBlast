#pragma once
#include "usb_mass_storage.h"
#include <array>

namespace oceanblast {
// OHCI control/bulk schedules for one virtual full-speed storage device.
// DMA touches bounded SDRAM only. Isochronous/periodic devices are unsupported.
class UsbOhci {
public:
    bool attach(const std::string& path) { const bool ok=device.attach(path);reset();return ok; }
    void reset() {
        regs.fill(0);regs[0]=0x10;regs[0x34/4]=0x2edf;regs[0x48/4]=0x200|2;
        regs[0x54/4]=device.attached()?0x10101:0x100;regs[0x58/4]=0x100;
        frameTicks=20000;resetTicks=0;done=0;doneInterrupt=false;device.reset();
    }
    u32 read(u32 offset) const { return offset<regs.size()*4&&!(offset&3)?regs[offset/4]:0; }
    void write(u32 offset,u32 value) {
        if(offset>=regs.size()*4||(offset&3))return;
        auto& r=regs[offset/4];
        switch(offset) {
        case 0: case 0x30: case 0x38: case 0x3c: case 0x48: case 0x4c: return;
        case 0x08:
            if(value&1){regs[0x04/4]=0;regs[0x08/4]=1;resetTicks=200;device.reset();}
            else r|=value&6;
            return;
        case 0x0c:r&=~value;return;
        case 0x10:r|=value;return;
        case 0x14:regs[0x10/4]&=~value;return;
        case 0x18:r=value&~255u;return;
        case 0x20:case 0x28:r=value&~15u;return;
        case 0x50:if(value&(1u<<16)){regs[0x54/4]|=256;regs[0x58/4]|=256;}return;
        case 0x54:case 0x58: {
            r&=~(value&0x1f0000u);
            if(value&1)r&=~2u;
            if(value&2 && r&1)r|=2;
            if(value&16 && r&1){r=(r|2|(1u<<20))&~16u;device.reset();regs[0x0c/4]|=64;}
            if(value&256)r|=256;
            if(value&512)r&=~256u;
            return;
        }
        default:r=value;return;
        }
    }
    bool irq() const { return (regs[0x10/4]&0x80000000u)&&(regs[0x0c/4]&regs[0x10/4]&0x7fffffffu); }
    bool needsTicks() const { return resetTicks||((regs[1]&0xc0)==0x80); }
    uint64_t ticksUntilEvent() const { return resetTicks?std::min(resetTicks,frameTicks):frameTicks; }
    void advance(size_t ticks,u8* ram,size_t ramSize,u32 ramBase) {
        if(resetTicks) {
            if(ticks>=resetTicks){resetTicks=0;regs[2]&=~1u;}
            else resetTicks-=ticks;
        }
        if((regs[1]&0xc0)!=0x80)return;
        while(ticks>=frameTicks){ticks-=frameTicks;frameTicks=20000;frame(ram,ramSize,ramBase);}
        frameTicks-=ticks;
    }
private:
    UsbMassStorage device;
    std::array<u32,24> regs{};
    size_t frameTicks=20000,resetTicks=0;
    u32 done=0;bool doneInterrupt=false;
    static bool valid(u32 p,size_t n,size_t size,u32 base) { return p>=base&&uint64_t(p)-base+n<=size; }
    static bool readWord(u32 p,u32& v,u8* ram,size_t size,u32 base) {
        if((p&3)||!valid(p,4,size,base))return false;
        std::memcpy(&v,ram+p-base,4);return true;
    }
    static bool writeWord(u32 p,u32 v,u8* ram,size_t size,u32 base) {
        if((p&3)||!valid(p,4,size,base))return false;
        std::memcpy(ram+p-base,&v,4);return true;
    }
    // A general TD may cross to a non-contiguous second 4-KiB page.
    static bool buffer(u32 current,u32 end,std::vector<u32>& addresses,size_t size,u32 base) {
        addresses.clear();if(!current)return true;
        const u32 first=4096-(current&4095);
        const uint64_t count=(current>>12)==(end>>12)?(end>=current?uint64_t(end)-current+1:8193):uint64_t(first)+(end&4095)+1;
        if(count>8192)return false;
        for(size_t i=0;i<count;++i) {
            const u32 p=i<first?current+u32(i):(end&~4095u)+u32(i-first);
            if(!valid(p,1,size,base))return false;
            addresses.push_back(p);
        }
        return true;
    }
    void schedule(u32 head,u8* ram,size_t size,u32 base) {
        unsigned budget=256;
        for(unsigned edges=0;head&&edges<256;++edges) {
            u32 info,tail,tdHead,next;
            if(!readWord(head,info,ram,size,base)||!readWord(head+4,tail,ram,size,base)||
               !readWord(head+8,tdHead,ram,size,base)||!readWord(head+12,next,ram,size,base)){regs[3]|=16;return;}
            if(!(info&((1u<<14)|(1u<<15)))&&!(tdHead&1)&&(info&127)==device.deviceAddress()) {
                while((tdHead&~15u)!=(tail&~15u)&&budget) {
                    --budget;const u32 td=tdHead&~15u;u32 flags,current,tdNext,end;
                    if(!readWord(td,flags,ram,size,base)||!readWord(td+4,current,ram,size,base)||
                       !readWord(td+8,tdNext,ram,size,base)||!readWord(td+12,end,ram,size,base)){regs[3]|=16;return;}
                    std::vector<u32> addresses;
                    if(!buffer(current,end,addresses,size,base)){regs[3]|=16;return;}
                    std::vector<u8> packet(addresses.size());
                    const unsigned dir=(info>>11)&3,pid=dir?dir:(flags>>19)&3,ep=(info>>7)&15;
                    if(pid!=2)for(size_t i=0;i<addresses.size();++i)packet[i]=ram[addresses[i]-base];
                    size_t actual=0;auto result=device.transfer(u8(ep),pid,packet,actual);
                    if(result==UsbMassStorage::Result::Nak)break;
                    u32 cc=result==UsbMassStorage::Result::Stall?4u:0u;
                    if(!cc&&actual<packet.size()&&!(flags&(1u<<18)))cc=9;
                    if(pid==2&&!cc)for(size_t i=0;i<actual;++i)ram[addresses[i]-base]=packet[i];
                    writeWord(td,(flags&0x0fffffffu)|(cc<<28),ram,size,base);
                    writeWord(td+4,actual==addresses.size()?0:addresses[actual],ram,size,base);
                    writeWord(td+8,done,ram,size,base);done=td;doneInterrupt|=((flags>>21)&7)!=7;
                    const unsigned mps=std::max(1u,(info>>16)&2047),toggle=(flags>>24)&3;
                    u32 carry=toggle&2?(toggle&1)<<1:tdHead&2;
                    if((std::max<size_t>(1,(actual+mps-1)/mps)&1)!=0)carry^=2;
                    tdHead=(tdNext&~15u)|carry|(cc?1:0);
                    writeWord(head+8,tdHead,ram,size,base);
                    if(cc||!budget)break;
                }
            }
            head=next&~15u;
        }
    }
    void frame(u8* ram,size_t size,u32 base) {
        regs[0x3c/4]=(regs[0x3c/4]+1)&65535;regs[3]|=4;
        const u32 hcca=regs[0x18/4];
        if(hcca&&valid(hcca,256,size,base)) {
            const u16 number=u16(regs[0x3c/4]);std::memcpy(ram+hcca+128-base,&number,2);
        }
        if(device.attached()&&(regs[0x54/4]&2)) {
            if(regs[1]&16)schedule(regs[0x20/4],ram,size,base);
            if(regs[1]&32)schedule(regs[0x28/4],ram,size,base);
        }
        // Do not overwrite an unacknowledged done queue in guest HCCA.
        if(done&&doneInterrupt&&!(regs[3]&2)&&hcca&&writeWord(hcca+132,done,ram,size,base)) {
            regs[0x30/4]=done;done=0;doneInterrupt=false;regs[3]|=2;
        }
    }
};
}
