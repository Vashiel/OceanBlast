// Sample guest work in fixed peripheral-time intervals; captures stay local.
#include "cpu/arm920t.h"
#include "core/input_script.h"
#include "kernel_jiffies.h"
#include <fstream>
#include <iostream>
#include <map>
#include <filesystem>
#include <chrono>
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
    bus.setMmioProfiling(true);
    uint64_t samples=0;bus.setAudioCallback([&](const int16_t*,size_t count){samples+=count;});
    std::ofstream intervals(output/"intervals.csv"),pages(output/"pages.csv"),frames(output/"frames.csv"),pcs(output/"pcs.csv");
    if(!intervals||!pages||!frames||!pcs)return 1;
    std::ofstream timing(output/"timing.csv"),mmio(output/"mmio.csv"),top(output/"top_pcs.csv"),syscalls(output/"syscalls.csv");
    if(!timing||!mmio||!top||!syscalls)return 1;
    timing<<"ticks,steps,host_seconds,jiffies_address,jiffies,jiffies_delta,timer4_expirations,timer4_requests,timer4_already_pending,timer4_irq_entries,timer4_source_clears,timer4_selected_clears,irq_masked_samples,lcd_irq_entries\n";
    mmio<<"ticks,physical_address,guest_reads,guest_writes,observed_value\n";
    top<<"ticks,rank,ttb,pc,samples,total_samples\n";
    syscalls<<"steps,ticks,ttb,pc,number,r0,r1,r2\n";
    uint64_t currentStep=0;
    cpu.setSyscallObserver([&](u32 nr,u32 pc,const std::array<u32,6>& args){
        if(nr!=5&&nr!=11&&nr!=54)return; // open, execve, ioctl; no full trace overhead.
        syscalls<<currentStep<<','<<currentStep/ratio<<",0x"<<std::hex<<bus.getTtb()<<",0x"<<pc<<std::dec<<','<<nr
                <<",0x"<<std::hex<<args[0]<<",0x"<<args[1]<<",0x"<<args[2]<<std::dec<<'\n';
    });
    auto started=std::chrono::steady_clock::now();
    std::map<u32,Bus::MmioAccesses> previousMmio;
    uint64_t timerEntries=0,lcdEntries=0,maskedSamples=0;
    u32 jiffiesAddress=0,lastJiffies=0; bool haveJiffies=false;
    intervals<<"ticks,steps,framebuffer_changes,pcm_samples,user_samples,kernel_samples,estimated_cycles,sampled_instructions,pc,ttb\n";
    pages<<"ticks,ttb,pc_page,samples\n";
    pcs<<"ticks,ttb,pc,samples\n";
    frames<<"ticks,steps,framebuffer,hash,steps_since_change\n";
    std::map<std::pair<u32,u32>,uint64_t> counts,pcCounts;
    uint64_t user=0,kernel=0,cycles=0,observations=0,changes=0,lastChange=0;
    uint32_t oldHash=0,oldAddress=0;size_t nextInput=0;
    constexpr uint64_t period=200000,report=20000000;
    for(uint64_t step=0;step<limit*ratio&&!cpu.isHalted();++step){
        currentStep=step;
        if(nextInput<input.size()&&step==input[nextInput].step)bus.setButtonMask(input[nextInput++].mask);
        if(step%1009==0){
            ++counts[{bus.getTtb(),cpu.getPC()&~0xfffu}];
            ++pcCounts[{bus.getTtb(),cpu.getPC()}];
            if((cpu.getCPSR()&31)==0x10)++user;else++kernel;
            ++observations;
            if(cpu.getCPSR()&FLAG_I)++maskedSamples;
        }
        if(!(cpu.getCPSR()&FLAG_I)&&bus.hasPendingIrq()) {
            const auto selected=bus.getMmio(0x4a000010);
            if(selected&(1u<<14))++timerEntries;
            if(selected&(1u<<16))++lcdEntries;
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
            if(!jiffiesAddress&&ticks>=3*report)jiffiesAddress=findKernelJiffies(bus);
            u32 jiffies=0; const bool valid=readKernelJiffies(bus,jiffiesAddress,jiffies);
            const auto& irq=bus.getIrqDiagnostics();
            timing<<ticks<<','<<step+1<<','<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()
                  <<",0x"<<std::hex<<jiffiesAddress<<std::dec<<',';
            if(valid)timing<<jiffies;
            timing<<',';if(valid&&haveJiffies)timing<<u32(jiffies-lastJiffies);
            timing<<','<<bus.getTimer4Expirations()<<','<<irq.requests[14]<<','<<irq.alreadyPending[14]<<','<<timerEntries
                  <<','<<irq.sourceClears[14]<<','<<irq.selectedClears[14]<<','<<maskedSamples<<','<<lcdEntries<<'\n';
            if(valid)lastJiffies=jiffies;
            haveJiffies=valid;
            for(const auto& item:bus.getMmioProfile()) {
                auto& previous=previousMmio[item.first];
                auto reads=item.second.reads-previous.reads,writes=item.second.writes-previous.writes;
                if(reads||writes)mmio<<ticks<<",0x"<<std::hex<<item.first<<std::dec<<','<<reads<<','<<writes
                                    <<",0x"<<std::hex<<bus.getMmio(item.first)<<std::dec<<'\n';
                previous=item.second;
            }
            std::vector<std::pair<std::pair<u32,u32>,uint64_t>> ranked(pcCounts.begin(),pcCounts.end());
            std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.second>b.second;});
            for(size_t i=0;i<std::min(size_t(10),ranked.size());++i)top<<ticks<<','<<i+1<<",0x"<<std::hex<<ranked[i].first.first
                <<",0x"<<ranked[i].first.second<<std::dec<<','<<ranked[i].second<<','<<observations<<'\n';
            intervals<<ticks<<','<<step+1<<','<<changes<<','<<samples<<','<<user<<','<<kernel<<','<<cycles<<','<<observations
                     <<",0x"<<std::hex<<cpu.getPC()<<",0x"<<bus.getTtb()<<std::dec<<'\n';
            for(const auto& c:counts)pages<<ticks<<",0x"<<std::hex<<c.first.first<<",0x"<<c.first.second<<std::dec<<','<<c.second<<'\n';
            for(const auto& c:pcCounts)pcs<<ticks<<",0x"<<std::hex<<c.first.first<<",0x"<<c.first.second<<std::dec<<','<<c.second<<'\n';
            counts.clear();pcCounts.clear();user=kernel=cycles=observations=changes=0;
            maskedSamples=0;
            timing.flush();mmio.flush();syscalls.flush();top.flush();
        }
    }
    std::ofstream memory(output/"sdram.bin",std::ios::binary);
    memory.write(reinterpret_cast<const char*>(bus.getSdramPtr()),ADDR_SDRAM_SIZE);
    if(!memory)return 1;
    cpu.dumpState();
    std::cout<<"PCM samples: "<<samples<<"; observations are not measured complete frames.\n";
    return !intervals||!pages||!frames||!pcs||!timing||!mmio||!top||!syscalls?1:0;
}
