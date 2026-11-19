#include "audio/audio.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace oceanblast;
int main() {
    Audio audio;
    if(!audio.init()){std::cerr<<"Audio device unavailable; playback position is unverified.\n";return 1;}
    uint64_t before=0,after=0;
    if(!audio.getPlayedFrames(before)||before!=0)return 1;
    std::vector<int16_t> silence(8192,0);
    const auto channels = static_cast<size_t>(audio.getChannels());
    const auto total = silence.size() / channels;
    audio.writeSamples(silence.data(), channels, audio.getSampleRate());
    if (audio.getProducedFrames() != 1 || audio.getPendingFrames() != 1 || audio.getSubmittedFrames() != 0) return 1;
    audio.writeSamples(silence.data() + channels, silence.size() - channels, audio.getSampleRate());
    if (audio.getProducedFrames() != total) return 1;
    uint64_t last=0;
    for(unsigned i=0;i<100;++i){
        if(!audio.getPlayedFrames(after)||after<last||after>audio.getSubmittedFrames())return 1;
        last=after;if(after==total)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(after!=total||audio.getSubmittedFrames()!=total||audio.getPendingFrames()||audio.getDroppedSamples())return 1;
    std::cout<<"PASS produced, pending, submitted and played frames follow worker delivery\n";
    audio.close();
    if(audio.getPlayedFrames(after))return 1;
    if(!audio.init()||!audio.getPlayedFrames(after)||after!=0||audio.getProducedFrames()||audio.getSubmittedFrames())return 1;
    std::cout<<"PASS reopened audio device resets playback position\n";
    return 0;
}
