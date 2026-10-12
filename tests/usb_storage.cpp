#include "memory/usb_ohci.h"
#include <filesystem>
#include <iostream>
using namespace oceanblast;
int main() {
    int failures=0;
    auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    const auto image=std::filesystem::temp_directory_path()/"oceanblast-usb-storage-test.img";
    if(std::filesystem::exists(image)) {std::cerr<<"Test image already exists.\n";return 1;}
    {std::ofstream f(image,std::ios::binary);std::vector<char> bytes(4096,0);f.write(bytes.data(),bytes.size());}
    {
        UsbMassStorage disk;check("USB attaches sector-aligned image",disk.attach(image.string())&&disk.capacity()==4096);
        size_t actual=0;
        std::vector<u8> setup={0x80,6,0,1,0,0,18,0};
        check("USB device descriptor setup",disk.transfer(0,0,setup,actual)==UsbMassStorage::Result::Success&&actual==8);
        std::vector<u8> descriptor(18);
        check("USB device descriptor response",disk.transfer(0,2,descriptor,actual)==UsbMassStorage::Result::Success&&actual==18&&descriptor[0]==18&&descriptor[7]==64);
        setup={0,9,1,0,0,0,0,0};disk.transfer(0,0,setup,actual);
        auto cbw=[&](u8 command,u32 lba,bool in){std::vector<u8> p(31,0);p[0]=0x55;p[1]=0x53;p[2]=0x42;p[3]=0x43;p[4]=0x36;p[8]=0;p[9]=2;p[12]=in?128:0;p[14]=10;p[15]=command;p[20]=u8(lba);p[23]=1;return p;};
        auto write=cbw(0x2a,2,false);check("USB accepts WRITE10 command",disk.transfer(2,1,write,actual)==UsbMassStorage::Result::Success);
        std::vector<u8> payload(512);for(unsigned i=0;i<512;++i)payload[i]=u8(i^0x5a);
        check("USB writes one bounded sector",disk.transfer(2,1,payload,actual)==UsbMassStorage::Result::Success&&actual==512);
        std::vector<u8> csw(13);disk.transfer(1,2,csw,actual);check("USB write reports matching successful CSW",actual==13&&csw[4]==0x36&&csw[12]==0&&csw[8]==0&&csw[9]==0);
        auto read=cbw(0x28,2,true);disk.transfer(2,1,read,actual);std::vector<u8> result(512);
        check("USB reads persisted sector",disk.transfer(1,2,result,actual)==UsbMassStorage::Result::Success&&actual==512&&result==payload);
        disk.transfer(1,2,csw,actual);
        auto bad=cbw(0x28,8,true);disk.transfer(2,1,bad,actual);disk.transfer(1,2,csw,actual);
        check("USB rejects out-of-range LBA with residue",csw[12]==1&&csw[9]==2);
        read[0]=0;check("USB rejects malformed CBW",disk.transfer(2,1,read,actual)==UsbMassStorage::Result::Stall);
    }
    {
        UsbMassStorage disk;check("USB data survives reopen",disk.attach(image.string()));
        std::ifstream f(image,std::ios::binary);f.seekg(1024);char first=0;f.read(&first,1);check("USB image contains guest write",u8(first)==0x5a);
    }
    {
        UsbOhci hc;hc.reset();std::vector<u8> ram(65536,0);constexpr u32 base=0x30000000;
        check("OHCI revision and disconnected root port",hc.read(0)==0x10&&!(hc.read(0x54)&1));
        hc.write(8,1);check("OHCI reset initially pending",hc.read(8)&1);
        hc.advance(200,ram.data(),ram.size(),base);check("OHCI reset completes in modeled time",!(hc.read(8)&1));
        check("OHCI attaches virtual device",hc.attach(image.string())&&(hc.read(0x54)&1));
        auto put=[&](u32 offset,u32 value){std::memcpy(ram.data()+offset,&value,4);};
        auto word=[&](u32 offset){u32 value;std::memcpy(&value,ram.data()+offset,4);return value;};
        hc.write(0x54,16);check("OHCI root-port reset enables device",(hc.read(0x54)&0x100002)==0x100002);
        hc.write(0x54,0x1f0000);hc.write(0x0c,~0u);
        hc.write(0x18,base);hc.write(0x20,base+0x100);
        put(0x100,64u<<16);put(0x104,base+0x240);put(0x108,base+0x200);put(0x10c,0);
        // SETUP / IN / OUT status, followed by an empty tail descriptor.
        const std::array<u32,3> flags={0xf2000000u,0xf3140000u,0xf3080000u};
        const std::array<u32,3> tds={0x200,0x210,0x220};
        const std::array<u32,3> buffers={0x300,0x400,0};
        const std::array<u32,3> ends={0x307,0x411,0};
        for(unsigned i=0;i<3;++i){put(tds[i],flags[i]);put(tds[i]+4,buffers[i]?base+buffers[i]:0);put(tds[i]+8,base+(i==2?0x240:tds[i+1]));put(tds[i]+12,ends[i]?base+ends[i]:0);}
        const u8 setup[]={128,6,0,1,0,0,18,0};std::copy(std::begin(setup),std::end(setup),ram.begin()+0x300);
        hc.write(0x10,0x80000002);hc.write(4,0x90);hc.advance(20000,ram.data(),ram.size(),base);
        check("OHCI enumerates descriptor through guest DMA",ram[0x400]==18&&ram[0x407]==64);
        check("OHCI completes TDs and publishes done head",(word(0x200)>>28)==0&&(word(0x210)>>28)==0&&(word(0x108)&~15u)==base+0x240&&word(132)==base+0x220);
        check("OHCI completion asserts enabled interrupt",hc.irq());hc.write(0x0c,2);check("OHCI interrupt acknowledgement clears source",!hc.irq());
        // Invalid DMA must not dereference a host pointer or consume the TD.
        put(0x108,base+0x200);put(0x204,base+0x10000);put(0x20c,base+0x10007);
        hc.advance(20000,ram.data(),ram.size(),base);check("OHCI rejects DMA outside SDRAM",(hc.read(0x0c)&16)&&(word(0x108)&~15u)==base+0x200);
    }
    std::filesystem::remove(image);
    return failures?1:0;
}
