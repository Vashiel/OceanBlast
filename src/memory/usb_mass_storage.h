#pragma once
#include "../core/types.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace oceanblast {
// One explicitly attached image, never a host drive or directory. USB Bulk-Only
// Transport and the small SCSI subset used by the cartridge's Linux driver.
class UsbMassStorage {
public:
    enum class Result { Success, Nak, Stall };
    bool attach(const std::string& path) {
        disk.close(); disk.clear(); bytes=0;
        disk.open(path, std::ios::in|std::ios::out|std::ios::binary|std::ios::ate);
        const auto length=disk.tellg();
        if (!disk || length < 512 || uint64_t(length)%512 || uint64_t(length)/512 > 0xffffffffu) {
            disk.close(); return false;
        }
        bytes=uint64_t(length); reset(); return true;
    }
    bool attached() const { return disk.is_open(); }
    uint64_t capacity() const { return bytes; }
    void reset() { address=configuration=0; pendingAddress=0; addressPending=false; control.clear(); controlOffset=0; resetBulk(); }
    void resetBulk() { phase=Phase::Command; data.clear(); offset=0; transferred=0; status=0; }
    u8 deviceAddress() const { return address; }
    Result transfer(u8 endpoint, unsigned pid, std::vector<u8>& packet, size_t& actual) {
        actual=0;
        if (!attached()) return Result::Nak;
        if (endpoint==0) return controlTransfer(pid,packet,actual);
        if (!configuration) return Result::Stall;
        if(endpoint==2&&pid==1) {
            if(phase==Phase::Command) {
                if(packet.size()!=31 || little(packet.data())!=0x43425355 || packet[13] ||
                   !packet[14] || packet[14]>16 || (packet[12]&0x7f)) return Result::Stall;
                tag=little(packet.data()+4); requested=little(packet.data()+8); in=(packet[12]&128)!=0;
                if(requested>1024*1024) return Result::Stall;
                transferred=0;offset=0;status=0;data.clear(); writeOffset=0;
                command(packet.data()+15); actual=31;
                return Result::Success;
            }
            if(phase!=Phase::Output) return Result::Stall;
            if(packet.size()>data.size()-offset) {status=2;phase=Phase::Status;return Result::Stall;}
            std::copy(packet.begin(),packet.end(),data.begin()+offset);offset+=packet.size();transferred+=packet.size();actual=packet.size();
            if(offset==data.size()) {
                disk.clear();disk.seekp(writeOffset);disk.write(reinterpret_cast<const char*>(data.data()),data.size());disk.flush();
                if(!disk){status=1;senseKey=3;senseAsc=0x0c;} phase=Phase::Status;
            }
            return Result::Success;
        }
        if(endpoint==1&&pid==2) {
            if(phase==Phase::Input) {
                actual=std::min(packet.size(),data.size()-offset);
                std::copy_n(data.begin()+offset,actual,packet.begin());offset+=actual;transferred+=actual;
                if(offset==data.size())phase=Phase::Status;
                return Result::Success;
            }
            if(phase==Phase::Status) {
                if(packet.size()<13)return Result::Stall;
                putLittle(packet.data(),0x53425355);putLittle(packet.data()+4,tag);
                putLittle(packet.data()+8,requested-std::min(requested,transferred));packet[12]=status;actual=13;
                phase=Phase::Command;return Result::Success;
            }
            return Result::Nak;
        }
        return Result::Stall;
    }
private:
    enum class Phase { Command, Input, Output, Status };
    std::fstream disk;
    uint64_t bytes=0,writeOffset=0;
    u8 address=0,configuration=0,pendingAddress=0,senseKey=0,senseAsc=0,status=0;
    bool addressPending=false,in=false;
    uint32_t tag=0,requested=0,transferred=0;
    Phase phase=Phase::Command;
    std::vector<u8> control,data;
    size_t controlOffset=0,offset=0;
    static u32 little(const u8* p) { return u32(p[0])|(u32(p[1])<<8)|(u32(p[2])<<16)|(u32(p[3])<<24); }
    static u32 big(const u8* p) { return (u32(p[0])<<24)|(u32(p[1])<<16)|(u32(p[2])<<8)|p[3]; }
    static void putLittle(u8* p,u32 n) { for(unsigned i=0;i<4;++i)p[i]=u8(n>>(8*i)); }
    static void putBig(u8* p,u32 n) { for(unsigned i=0;i<4;++i)p[i]=u8(n>>(24-8*i)); }
    void fail(u8 asc) { status=1;senseKey=5;senseAsc=asc;data.clear();phase=Phase::Status; }
    void command(const u8* cdb) {
        phase=Phase::Status;
        switch(cdb[0]) {
        case 0x00: case 0x1b: case 0x1e: case 0x35: break;
        case 0x12:
            data.assign(36,0);data[1]=128;data[2]=2;data[3]=2;data[4]=31;
            std::memcpy(data.data()+8,"OceanBst",8);std::memcpy(data.data()+16,"MP3 storage     ",16);std::memcpy(data.data()+32,"0001",4);
            data.resize(std::min<size_t>(data.size(),cdb[4]));break;
        case 0x03:
            data.assign(18,0);data[0]=0x70;data[2]=senseKey;data[7]=10;data[12]=senseAsc;
            data.resize(std::min<size_t>(data.size(),cdb[4]));senseKey=senseAsc=0;break;
        case 0x25:
            data.assign(8,0);putBig(data.data(),u32(bytes/512-1));putBig(data.data()+4,512);break;
        case 0x23:
            data.assign(12,0);data[3]=8;putBig(data.data()+4,u32(bytes/512));data[8]=2;data[10]=2;break;
        case 0x1a: data={3,0,0,0};data.resize(std::min<size_t>(data.size(),cdb[4]));break;
        case 0x5a: data={0,6,0,0,0,0,0,0};break;
        case 0x28: case 0x2a: {
            const uint64_t position=uint64_t(big(cdb+2))*512;
            const size_t length=size_t((u32(cdb[7])<<8)|cdb[8])*512;
            if(position>bytes||length>bytes-position||length>1024*1024){fail(0x21);return;}
            if(length!=requested || (cdb[0]==0x28)!=in){status=2;return;}
            data.assign(length,0);writeOffset=position;
            if(cdb[0]==0x2a){phase=length?Phase::Output:Phase::Status;return;}
            disk.clear();disk.seekg(position);disk.read(reinterpret_cast<char*>(data.data()),length);
            if(!disk){fail(0x11);senseKey=3;return;}break;
        }
        default: fail(0x20);return;
        }
        if(!data.empty()) {
            if(!in){status=2;data.clear();return;}
            data.resize(std::min<size_t>(data.size(),requested));phase=data.empty()?Phase::Status:Phase::Input;
        } else if(requested) status=2;
    }
    Result controlTransfer(unsigned pid,std::vector<u8>& packet,size_t& actual) {
        if(pid==0) {
            if(packet.size()!=8)return Result::Stall;
            control.clear();controlOffset=0;addressPending=false;
            const unsigned type=packet[0],request=packet[1],value=packet[2]|(unsigned(packet[3])<<8),length=packet[6]|(unsigned(packet[7])<<8);
            if(request==6&&type==0x80) {
                if((value>>8)==1)control={18,1,0x10,1,0,0,0,64,0,0,1,0,0,1,1,2,0,1};
                else if((value>>8)==2)control={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,64,0,0,7,5,2,2,64,0,0};
                else if((value>>8)==3) {
                    if(!(value&255))control={4,3,9,4};
                    else {const std::string name=(value&255)==1?"OceanBlast":"MP3 storage";control={u8(2+name.size()*2),3};for(char ch:name){control.push_back(u8(ch));control.push_back(0);}}
                } else return Result::Stall;
            } else if(request==5&&type==0&&value<128){pendingAddress=u8(value);addressPending=true;}
            else if(request==9&&type==0&&value<=1)configuration=u8(value);
            else if(request==8&&type==0x80)control={configuration};
            else if(request==0&&(type&128))control={0,0};
            else if(request==1&&!(type&128)){}
            else if(request==0xfe&&type==0xa1)control={0};
            else if(request==0xff&&type==0x21)resetBulk();
            else return Result::Stall;
            if(control.size()>length)control.resize(length);
            actual=8;return Result::Success;
        }
        if(pid==2&&!packet.empty()) {
            actual=std::min(packet.size(),control.size()-controlOffset);
            std::copy_n(control.begin()+controlOffset,actual,packet.begin());controlOffset+=actual;return Result::Success;
        }
        if(packet.empty()&&(pid==1||pid==2)) {
            if(addressPending){address=pendingAddress;addressPending=false;}
            return Result::Success;
        }
        return Result::Stall;
    }
};
}
