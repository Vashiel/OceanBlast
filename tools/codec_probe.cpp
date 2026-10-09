// Observe a guest decoder export and its returns without changing guest state.
#include "cpu/arm920t.h"
#include "core/input_script.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace oceanblast;
struct Call { uint64_t step; u32 ttb,sp,returnPc,buffer,parameters; };
static u32 findTransform(Bus& bus) {
    auto word=[&](u32 address,u32& value){return bus.peek32(address,value);};
    for(u32 address=0x40000000;address<0x41000000;address+=4096){
        u32 magic=0,offset=0,layout=0,count=0;
        if(!word(address,magic)||magic!=0x464c457f||!word(address+28,offset)||
           !word(address+40,layout)||!word(address+44,count))continue;
        const u32 phSize=layout>>16,phCount=count&65535;
        if(phSize<32||phCount>64||!phCount||offset>65536)continue;
        u32 minimum=~0u,dynamic=0,dynamicSize=0;
        for(u32 i=0;i<phCount;++i){
            u32 type=0,va=0,size=0;
            if(!word(address+offset+i*phSize,type)||!word(address+offset+i*phSize+8,va)||!word(address+offset+i*phSize+16,size))continue;
            if(type==1)minimum=std::min(minimum,va&~4095u);
            if(type==2){dynamic=va;dynamicSize=size;}
        }
        if(minimum==~0u||!dynamic||dynamicSize>32768)continue;
        const u32 base=address-minimum;u32 strings=0,symbols=0,hash=0;
        for(u32 i=0;i<dynamicSize;i+=8){
            u32 tag=0,value=0;if(!word(base+dynamic+i,tag)||!word(base+dynamic+i+4,value)||!tag)break;
            const u32 pointer=value<base?base+value:value;
            if(tag==4)hash=pointer;else if(tag==5)strings=pointer;else if(tag==6)symbols=pointer;
        }
        u32 names=0;if(!strings||!symbols||!hash||!word(hash+4,names)||names>100000)continue;
        for(u32 i=0;i<names;++i){
            u32 name=0,value=0,info=0;
            if(!word(symbols+16*i,name)||!word(symbols+16*i+4,value)||!word(symbols+16*i+12,info)||!name||!(info>>16)||(info&15)!=2)continue;
            constexpr char wanted[]="RV40toYUV420Transform";bool match=true;
            for(size_t byte=0;byte<sizeof(wanted);++byte){u8 actual=0;if(!bus.peek8(strings+name+u32(byte),actual)||actual!=wanted[byte]){match=false;break;}}
            if(match)return base+value;
        }
    }
    return 0;
}
int main(int argc,char** argv) {
    if(argc!=8){std::cerr<<"Usage: codec_probe ROM STEPS RATIO OUTPUT_DIRECTORY EEPROM INPUT_SCRIPT TRANSFORM_PC_OR_AUTO\n";return 1;}
    uint64_t limit;size_t ratio;u32 entry;
    try{limit=std::stoull(argv[2]);ratio=std::stoull(argv[3]);entry=std::string(argv[7])=="auto"?0:std::stoul(argv[7],nullptr,0);}catch(...){return 1;}
    if(!limit||!ratio||ratio>16)return 1;
    const std::filesystem::path output(argv[4]);
    if(!std::filesystem::create_directory(output))return 1;
    Bus bus;if(!bus.loadEeprom(argv[5])||!bus.loadCartridge(argv[1]))return 1;
    std::ifstream script(argv[6]);std::vector<InputEvent> inputs;std::string error;
    if(!script||!readInputScript(script,inputs,error))return 1;
    ARM920T cpu(bus);std::vector<Call> calls;size_t nextInput=0,images=0;
    uint64_t entries=0,returns=0;
    std::ofstream records(output/"codec.csv");
    records<<"entry_step,return_step,ttb,result,output_buffer,out0,out1,out2,width,height,out5,yuv_bytes,nonzero_yuv_bytes,luma_min,luma_max,luma_hash,chroma_min,chroma_max\n";
    for(uint64_t step=0;step<limit&&!cpu.isHalted();++step){
        if(!entry&&step%25000000==0){entry=findTransform(bus);if(entry)std::cout<<"Transform export: 0x"<<std::hex<<entry<<"; TTB: 0x"<<bus.getTtb()<<std::dec<<'\n';}
        if(nextInput<inputs.size()&&step==inputs[nextInput].step)bus.setButtonMask(inputs[nextInput++].mask);
        if((cpu.getCPSR()&31)==0x10){
            for(auto it=calls.begin();it!=calls.end();){
                if(cpu.getPC()!=it->returnPc||cpu.getSP()!=it->sp||bus.getTtb()!=it->ttb){++it;continue;}
                u32 values[6]={};bool valid=true;
                for(unsigned i=0;i<6;++i)valid=bus.peek32(it->parameters+4*i,values[i])&&valid;
                const uint64_t pixels=uint64_t(values[3])*values[4];
                const size_t size=valid&&values[3]&&values[4]&&pixels<=1024*1024?size_t(pixels*3/2):0;
                std::vector<u8> frame(size);size_t nonzero=0;
                for(size_t i=0;i<size;++i){if(!bus.peek8(it->buffer+u32(i),frame[i])){valid=false;break;}nonzero+=frame[i]!=0;}
                records<<it->step<<','<<step<<",0x"<<std::hex<<it->ttb<<",0x"<<cpu.getReg(0)<<",0x"<<it->buffer<<std::dec;
                for(u32 value:values)records<<','<<value;
                unsigned yMin=255,yMax=0,cMin=255,cMax=0;uint32_t hash=2166136261u;
                if(valid&&size){
                    for(size_t i=0;i<pixels;++i){yMin=std::min<unsigned>(yMin,frame[i]);yMax=std::max<unsigned>(yMax,frame[i]);hash=(hash^frame[i])*16777619u;}
                    for(size_t i=size_t(pixels);i<size;++i){cMin=std::min<unsigned>(cMin,frame[i]);cMax=std::max<unsigned>(cMax,frame[i]);}
                    std::ofstream last(output/"decoded_last.yuv",std::ios::binary);last.write(reinterpret_cast<const char*>(frame.data()),frame.size());if(!last)return 1;
                }
                records<<','<<(valid?size:0)<<','<<(valid?nonzero:0)<<','<<yMin<<','<<yMax<<",0x"<<std::hex<<hash<<std::dec<<','<<cMin<<','<<cMax<<'\n';
                records.flush();
                if(valid&&nonzero&&images<3){std::ofstream image(output/("decoded_"+std::to_string(images++)+".yuv"),std::ios::binary);image.write(reinterpret_cast<const char*>(frame.data()),frame.size());if(!image)return 1;}
                ++returns;it=calls.erase(it);
            }
            if(cpu.getPC()==entry){
                calls.push_back({step,bus.getTtb(),cpu.getSP(),cpu.getLR()&~1u,cpu.getReg(1),cpu.getReg(3)});
                ++entries;
            }
        }
        cpu.step(step%ratio==0?1:0);
    }
    std::ofstream memory(output/"sdram.bin",std::ios::binary);
    memory.write(reinterpret_cast<const char*>(bus.getSdramPtr()),ADDR_SDRAM_SIZE);
    const u32 framebuffer=(bus.getMmio(0x4d000014)&0x1fffffff)<<1;
    const size_t stride=std::max(bus.getFramebufferStride(),bus.isLcd16Bpp()?size_t(480):size_t(360));
    if(framebuffer>=ADDR_SDRAM_BASE&&uint64_t(framebuffer-ADDR_SDRAM_BASE)+stride*bus.getFramebufferHeight()<=ADDR_SDRAM_SIZE){
        std::ofstream frame(output/"fb_active.raw",std::ios::binary);
        frame.write(reinterpret_cast<const char*>(bus.getSdramPtr()+framebuffer-ADDR_SDRAM_BASE),stride*bus.getFramebufferHeight());if(!frame)return 1;
    }
    std::cout<<"Final TTB: 0x"<<std::hex<<bus.getTtb()<<"; framebuffer: 0x"<<framebuffer<<std::dec<<"; stride: "<<stride<<"; height: "<<bus.getFramebufferHeight()<<"; RGB565: "<<bus.isLcd16Bpp()<<'\n';
    std::cout<<"Decoder entries: "<<entries<<"; completed returns: "<<returns<<"; unfinished calls: "<<calls.size()<<'\n';
    cpu.dumpState();return records&&memory?0:1;
}
