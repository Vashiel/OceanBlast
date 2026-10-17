from pathlib import Path
import shutil
b=Path('build/2026-10-17-wade-startup');s=b/'trace-src';shutil.copytree('src',s,dirs_exist_ok=True)
p=s/'memory/bus.h';text=p.read_text();text=text.replace('namespace oceanblast {','namespace oceanblast {\nvoid startupTraceWrite(u32 pa, unsigned bytes);',1).replace('inline void captureFrameWrite(u32 pa, unsigned bytes) {','inline void captureFrameWrite(u32 pa, unsigned bytes) {\n        if ((pa - 0x30350000u) < 2048u) startupTraceWrite(pa, bytes);');p.write_text(text)
p=s/'memory/bus.cpp';text=p.read_text().replace('namespace oceanblast {','namespace oceanblast {\nvoid startupTraceRead(u32 pa, unsigned bytes);',1);needle='const auto* pcm = reinterpret_cast<const int16_t*>(sdram.data() + dma2Src - ADDR_SDRAM_BASE + dma2EmittedBytes);';assert needle in text;text=text.replace(needle,'startupTraceRead(dma2Src + dma2EmittedBytes, end - dma2EmittedBytes);\n            '+needle);p.write_text(text)
p=b/'capture.cpp';text=p.read_text().replace('#include "memory/bus.cpp"','#include "trace-src/memory/bus.cpp"').replace('#include "cpu/arm920t.cpp"','#include "trace-src/cpu/arm920t.cpp"').replace('#include "core/machine.h"','#include "trace-src/core/machine.h"').replace('#include "audio/resampler.h"','#include "trace-src/audio/resampler.h"')
hook='''
namespace oceanblast {
static uint64_t writeSequence=0, written[2048]{}, readVersion[2048]{}, staleBytes=0, freshBytes=0, neverWrittenBytes=0;
void startupTraceWrite(u32 pa,unsigned bytes) {
    for(unsigned i=0;i<bytes && pa+i-0x30350000u<2048u;++i) written[pa+i-0x30350000u]=++writeSequence;
}
void startupTraceRead(u32 pa,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) {
        const u32 index=pa+i-0x30350000u;if(index>=2048)continue;
        if(!written[index])++neverWrittenBytes;
        else if(written[index]==readVersion[index])++staleBytes;
        else ++freshBytes;
        readVersion[index]=written[index];
    }
}
}
'''
text=text.replace('int main(int argc,char** argv)',hook+'\nint main(int argc,char** argv)');text=text.replace('std::ofstream raw(prefix+".pcm"','std::ofstream ring(prefix+".ring.csv");ring<<"callback,stale_bytes,fresh_bytes,never_written_bytes\\n";\n    std::ofstream raw(prefix+".pcm"');text=text.replace('const auto rate=bus.getAudioSampleRate();','ring<<callbacks<<\',\'<<staleBytes<<\',\'<<freshBytes<<\',\'<<neverWrittenBytes<<\'\\n\';staleBytes=freshBytes=neverWrittenBytes=0;\n        const auto rate=bus.getAudioSampleRate();')
(b/'ring_probe.cpp').write_text(text)
