// Sample guest work in fixed peripheral-time intervals; captures stay local.
#include "cpu/arm920t.h"
#include "core/input_script.h"
#include <fstream>
#include <iostream>
#include <map>
#include <filesystem>
using namespace oceanblast;
int main(int argc,char** argv) {
    if(argc!=7){std::cerr<<"Usage: scene_work_probe ROM TICKS RATIO OUTPUT_DIRECTORY EEPROM INPUT_SCRIPT\n";return 1;}
    uint64_t limit=0;size_t ratio=0;
    try{limit=std::stoull(argv[2]);ratio=std::stoull(argv[3]);}catch(...){return 1;}
    if(!limit||!ratio||ratio>16||limit>UINT64_MAX/ratio)return 1;
    std::filesystem::path output(argv[4]);
    if(!std::filesystem::create_directory(output))return 1;
    Bus bus;if(!bus.loadEeprom(argv[5])||!bus.loadCartridge(argv[1]))return 1;
    std::vector<InputEvent> input;std::string error;std::ifstream script(argv[6]);
    if(!script||!readInputScript(script,input,error)){std::cerr<<error;return 1;}
    ARM920T cpu(bus);cpu.reset();
    uint64_t samples=0;bus.setAudioCallback([&](const int16_t*,size_t count){samples+=count;});
    std::ofstream intervals(output/"intervals.csv"),pages(output/"pages.csv"),frames(output/"frames.csv"),pcs(output/"pcs.csv");
    if(!intervals||!pages||!frames||!pcs)return 1;
    intervals<<"ticks,steps,framebuffer_changes,pcm_samples,user_samples,kernel_samples,estimated_cycles,sampled_instructions,pc,ttb\n";
    pages<<"ticks,ttb,pc_page,samples\n";
    pcs<<"ticks,ttb,pc,samples\n";
    frames<<"ticks,steps,framebuffer,hash,steps_since_change\n";
    std::map<std::pair<u32,u32>,uint64_t> counts,pcCounts;
    uint64_t user=0,kernel=0,cycles=0,observations=0,changes=0,lastChange=0;
    uint32_t oldHash=0,oldAddress=0;size_t nextInput=0;
    constexpr uint64_t period=200000,report=20000000;
    for(uint64_t step=0;step<limit*ratio&&!cpu.isHalted();++step){
        if(nextInput<input.size()&&step==input[nextInput].step)bus.setButtonMask(input[nextInput++].mask);
        if(step%1009==0){
            ++counts[{bus.getTtb(),cpu.getPC()&~0xfffu}];
            ++pcCounts[{bus.getTtb(),cpu.getPC()}];
            if((cpu.getCPSR()&31)==0x10)++user;else++kernel;
            ++observations;
        }
        cpu.step(step%ratio==0?1:0);
        if(step%1009==0)cycles+=cpu.getLastCycles();
        if((step+1)%(period*ratio))continue;
        const uint64_t ticks=(step+1)/ratio;
        const u32 address=(bus.getMmio(0x4d000014)&0x1fffffff)<<1;
        const size_t stride=std::max(bus.getFramebufferStride(),bus.isLcd16Bpp()?size_t(480):size_t(360));
        uint32_t hash=2166136261u;
        if(address>=ADDR_SDRAM_BASE&&uint64_t(address-ADDR_SDRAM_BASE)+stride*bus.getFramebufferHeight()<=ADDR_SDRAM_SIZE){
            const auto* data=bus.getSdramPtr()+address-ADDR_SDRAM_BASE;
            for(size_t byte=0;byte<stride*bus.getFramebufferHeight();++byte)hash=(hash^data[byte])*16777619u;
            if(ticks%report==0){std::ofstream frame(output/("frame_"+std::to_string(ticks)+".raw"),std::ios::binary);frame.write(reinterpret_cast<const char*>(data),stride*bus.getFramebufferHeight());}
        }else hash=0;
        if(hash!=oldHash||address!=oldAddress){
            ++changes;frames<<ticks<<','<<step+1<<",0x"<<std::hex<<address<<",0x"<<hash<<std::dec<<','<<step+1-lastChange<<'\n';
            lastChange=step+1;oldHash=hash;oldAddress=address;
        }
        if(ticks%report==0){
            intervals<<ticks<<','<<step+1<<','<<changes<<','<<samples<<','<<user<<','<<kernel<<','<<cycles<<','<<observations
                     <<",0x"<<std::hex<<cpu.getPC()<<",0x"<<bus.getTtb()<<std::dec<<'\n';
            for(const auto& c:counts)pages<<ticks<<",0x"<<std::hex<<c.first.first<<",0x"<<c.first.second<<std::dec<<','<<c.second<<'\n';
            for(const auto& c:pcCounts)pcs<<ticks<<",0x"<<std::hex<<c.first.first<<",0x"<<c.first.second<<std::dec<<','<<c.second<<'\n';
            counts.clear();pcCounts.clear();user=kernel=cycles=observations=changes=0;
        }
    }
    std::ofstream memory(output/"sdram.bin",std::ios::binary);
    memory.write(reinterpret_cast<const char*>(bus.getSdramPtr()),ADDR_SDRAM_SIZE);
    if(!memory)return 1;
    cpu.dumpState();
    std::cout<<"PCM samples: "<<samples<<"; observations are not measured complete frames.\n";
    return !intervals||!pages||!frames||!pcs?1:0;
}
