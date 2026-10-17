#include "memory/bus.cpp"
#include "cpu/arm920t.cpp"
#include "core/machine.h"
#include "audio/resampler.h"
#include <fstream>
#include <iostream>
using namespace oceanblast;
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    Machine machine;auto& bus=machine.bus();auto& cpu=machine.cpu();
    if(!bus.loadEeprom(argv[2])||!bus.loadCartridge(argv[1]))return 3;
    cpu.reset();const auto ratio=std::stoul(argv[4]);machine.configureExecution(false,ratio);
    const std::string prefix=argv[3];std::ofstream raw(prefix+".pcm",std::ios::binary),converted(prefix+".converted.pcm",std::ios::binary),events(prefix+".csv");
    PcmResampler resampler;uint64_t callbacks=0,samples=0,steps=0;
    events<<"callback,guest_ticks,instruction_batch_start,sample_offset,samples,input_rate,iismod,iispsr,iiscon,dcon,source,remaining,pc\n";
    bus.setAudioCallback([&](const int16_t* p,size_t n){
        const auto rate=bus.getAudioSampleRate();
        events<<callbacks++<<','<<machine.execution().time().ticks<<','<<steps<<','<<samples<<','<<n<<','<<rate<<','<<bus.getMmio(0x55000004)<<','<<bus.getMmio(0x55000008)<<','<<bus.getMmio(0x55000000)<<','<<bus.getMmio(0x4b000090)<<','<<bus.getMmio(0x4b000098)<<','<<bus.getMmio(0x4b000094)<<','<<cpu.getPC()<<'\n';
        raw.write(reinterpret_cast<const char*>(p),n*sizeof(int16_t));samples+=n;
        auto pcm=resampler.process(p,n,rate,22050,2);converted.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(int16_t));
    });
    while(machine.execution().time().ticks<320000000ULL&&!cpu.isHalted())steps+=machine.execution().run(4096,320000000ULL);
    std::ofstream ram(prefix+".sdram",std::ios::binary);ram.write(reinterpret_cast<const char*>(bus.getSdramPtr()),ADDR_SDRAM_SIZE);
    std::cout<<"steps="<<steps<<" callbacks="<<callbacks<<" samples="<<samples<<'\n';cpu.dumpState();
    return machine.execution().time().ticks==320000000ULL?0:4;
}
