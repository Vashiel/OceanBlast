// Measure host deadline wakeup jitter without a cartridge or guest clock.
#include "core/host_pacer.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>
using namespace oceanblast;
int main() {
    std::cout << "mode,high_resolution,samples,interval_us,mean_lateness_us,p95_lateness_us,max_lateness_us\n";
    for (bool precise : {false, true}) {
        HostPacer pacer(precise);
        std::vector<double> late;
        auto deadline = HostPacer::Clock::now();
        for (unsigned i = 0; i < 400; ++i) {
            deadline += std::chrono::microseconds(1250);
            pacer.waitUntil(deadline);
            late.push_back(std::chrono::duration<double, std::micro>(HostPacer::Clock::now()-deadline).count());
        }
        std::sort(late.begin(),late.end());
        std::cout << (precise ? "timer" : "sleep") << ',' << pacer.highResolution()
                  << ',' << late.size() << ",1250," << std::accumulate(late.begin(),late.end(),0.0)/late.size()
                  << ',' << late[379] << ',' << late.back() << '\n';
    }
}
