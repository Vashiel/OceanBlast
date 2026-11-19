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
    audio.writeSamples(silence.data(),silence.size(),audio.getSampleRate());
    uint64_t last=0;
    for(unsigned i=0;i<40;++i){
        if(!audio.getPlayedFrames(after)||after<last||after>audio.getSubmittedFrames())return 1;
        last=after;if(after==audio.getSubmittedFrames())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!after||after!=audio.getSubmittedFrames()||audio.getDroppedSamples())return 1;
    std::cout<<"PASS waveOut playback position advances through every submitted frame\n";
    audio.close();
    if(audio.getPlayedFrames(after))return 1;
    if(!audio.init()||!audio.getPlayedFrames(after)||after!=0)return 1;
    std::cout<<"PASS reopened audio device resets playback position\n";
    return 0;
}
