#include "audio/resampler.h"
#include <cmath>
#include <iostream>
using namespace oceanblast;
int main() {
    int failures = 0;
    std::vector<int16_t> input(20000);
    for (size_t i = 0; i < input.size() / 2; ++i) {
        input[i * 2] = static_cast<int16_t>(10000 * std::sin(i * 0.1));
        input[i * 2 + 1] = -input[i * 2];
    }
    for (const auto rates : {std::pair<int,int>{8000,22050}, {11025,22050}, {22050,44100}, {44100,22050}, {32000,22050}}) {
        PcmResampler whole, split;
        auto reference = whole.process(input.data(), input.size(), rates.first, rates.second, 2);
        std::vector<int16_t> actual;
        for (size_t start = 0; start < input.size(); start += 256) {
            auto block = split.process(input.data() + start, std::min(size_t(256), input.size() - start), rates.first, rates.second, 2);
            actual.insert(actual.end(), block.begin(), block.end());
        }
        bool ok = reference.size() == actual.size();
        for (size_t i = 0; ok && i < actual.size(); ++i) ok = std::abs(int(reference[i]) - actual[i]) <= 1;
        const double expected = (input.size() / 2 - 1) * double(rates.second) / rates.first;
        ok = ok && std::abs(double(actual.size() / 2) - expected) <= 1;
        std::cout << (ok ? "PASS " : "FAIL ") << rates.first << " -> " << rates.second << " streaming duration and buffer boundaries\n";
        if (!ok) ++failures;
    }
    return failures ? 1 : 0;
}
