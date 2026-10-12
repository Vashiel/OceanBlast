// Read-only SDL entry/return tracing for the known stripped Rayman 3 binary.
#include "cpu/arm920t.h"
#include "core/input_script.h"
#include <fstream>
#include <iostream>
#include <vector>
using namespace oceanblast;
struct Pending { u32 ttb, sp, pc, entry, surface; uint64_t step; };
int main(int argc, char** argv) {
    if (argc != 6) { std::cerr << "Usage: sdl_surface_probe ROM STEPS OUTPUT.csv EEPROM INPUT_SCRIPT\n"; return 1; }
    Bus bus;
    if (!bus.loadEeprom(argv[4]) || !bus.loadCartridge(argv[1])) return 1;
    std::ifstream script(argv[5]); std::vector<InputEvent> events; std::string error;
    if (!script || !readInputScript(script, events, error)) return 1;
    ARM920T cpu(bus); size_t next = 0; std::vector<Pending> calls;
    std::ofstream out(argv[3]);
    out << "step,phase,entry,ttb,return_pc,r0,r1,r2,r3,surface,flags,format,bpp,rmask,gmask,bmask,colorkey,alpha,pixels\n";
    auto record = [&](uint64_t step, const char* phase, u32 entry, u32 surface, u32 returnPc) {
        u32 flags=0,format=0,pixels=0,words[10]={};
        bus.peek32(surface,flags); bus.peek32(surface+4,format); bus.peek32(surface+20,pixels);
        for(unsigned i=0;i<10;++i)bus.peek32(format+4*i,words[i]);
        out << std::dec << step << ',' << phase << std::hex << ',' << entry << ',' << bus.getTtb() << ',' << returnPc;
        for(unsigned i=0;i<4;++i)out << ',' << cpu.getReg(i);
        out << ',' << surface << ',' << flags << ',' << format << ',' << ((words[1]>>0)&255)
            << ',' << words[4] << ',' << words[5] << ',' << words[6] << ',' << words[8] << ',' << (words[9]&255) << ',' << pixels << '\n';
    };
    const uint64_t limit=std::stoull(argv[2]);
    for(uint64_t step=0;step<limit&&!cpu.isHalted();++step) {
        if(next<events.size()&&step==events[next].step)bus.setButtonMask(events[next++].mask);
        if((cpu.getCPSR()&31)==0x10) {
            for(auto it=calls.begin();it!=calls.end();) {
                if(cpu.getPC()!=it->pc||cpu.getSP()!=it->sp||bus.getTtb()!=it->ttb){++it;continue;}
                const bool created=it->entry==0xb2dc||it->entry==0xb3cc||it->entry==0xbbcc;
                record(step,"return",it->entry,created?cpu.getReg(0):it->surface,it->pc);
                it=calls.erase(it);
            }
            const u32 pc=cpu.getPC();
            if(pc==0xb2dc||pc==0xb3cc||pc==0xbbcc||pc==0xb6dc||pc==0xb7cc||pc==0xb42c) {
                record(step,"entry",pc,cpu.getReg(0),cpu.getLR()&~1u);
                calls.push_back({bus.getTtb(),cpu.getSP(),cpu.getLR()&~1u,pc,cpu.getReg(0),step});
            }
        }
        cpu.step(step%4==0?1:0);
    }
    cpu.dumpState(); return !out;
}
