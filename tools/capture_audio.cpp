// Capture generated PCM without host playback. ROM and WAV contents stay local.
#include "cpu/arm920t.h"
#include "audio/resampler.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <limits>
using namespace oceanblast;
static void le(std::ostream& out,uint32_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)out.put(char(value>>(i*8)));
}
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5){std::cerr<<"Usage: capture_audio ROM STEPS OUTPUT.wav [CPU_STEPS_PER_PERIPHERAL_TICK]\n";return 1;}
    uint64_t limit=0;
    unsigned divider=1;
    try{limit=std::stoull(argv[2]);if(argc==5)divider=std::stoul(argv[4]);}catch(...){return 1;}
    if(!limit||!divider||divider>16)return 1;
    Bus bus;if(!bus.loadCartridge(argv[1]))return 1;
    ARM920T cpu(bus);cpu.reset();
    std::fstream wav(argv[3],std::ios::binary|std::ios::out|std::ios::trunc);
    std::ofstream events(std::string(argv[3])+".csv");
    if(!wav||!events)return 1;
    wav.write("RIFF",4);le(wav,36,4);wav.write("WAVEfmt ",8);le(wav,16,4);
    le(wav,1,2);le(wav,2,2);le(wav,22050,4);le(wav,22050*4,4);
    le(wav,4,2);le(wav,16,2);wav.write("data",4);le(wav,0,4);
    uint64_t step=0,bytes=0;
    bool overflow=false;
    PcmResampler resampler;
    events<<"step,output_byte_offset,input_rate,iismod,iispsr,dcon,current_source,remaining_items,samples,iiscon,fclk,pclk\n";
    bus.setAudioCallback([&](const int16_t* input,size_t count){
        const auto rate=bus.getAudioSampleRate();
        events<<step<<','<<bytes<<','<<rate<<",0x"<<std::hex<<bus.getMmio(0x55000004)
              <<",0x"<<bus.getMmio(0x55000008)<<",0x"<<bus.getMmio(0x4b000090)
              <<",0x"<<bus.getMmio(0x4b000098)<<','<<std::dec<<bus.getMmio(0x4b000094)<<','<<count
              <<','<<bus.getMmio(0x55000000)<<','<<bus.getCpuClock()<<','<<bus.getPeripheralClock()<<'\n';
        auto pcm=resampler.process(input,count,rate,22050,2);
        if(bytes+pcm.size()*2>std::numeric_limits<uint32_t>::max()-36){overflow=true;return;}
        for(auto sample:pcm)le(wav,uint16_t(sample),2);
        bytes+=pcm.size()*2;
    });
    for(;step<limit&&!cpu.isHalted()&&!overflow;++step)cpu.step(step%divider==0?1:0);
    wav.seekp(4);le(wav,uint32_t(36+bytes),4);wav.seekp(40);le(wav,uint32_t(bytes),4);
    wav.flush();events.flush();
    std::cout<<"Captured "<<bytes/4<<" stereo frames; "<<step<<" steps.\n";
    std::cout<<"Repeated enables during active DMA: "<<bus.getDma2RedundantEnables()<<"\n";
    std::cout<<"PCM capture excludes host queue starvation and does not establish audible acceptance.\n";
    return (!wav||!events||overflow)?1:0;
}
