#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace oceanblast {
// Streaming interpolation keeps the phase and boundary frame across DMA buffers.
class PcmResampler {
    std::vector<int16_t> pending;
    double position = 0;
    int inputRate = 0, outputRate = 0, channels = 0;
public:
    void reset() { pending.clear(); position = 0; inputRate = outputRate = channels = 0; }
    std::vector<int16_t> process(const int16_t* samples, size_t count, int inRate, int outRate, int channelCount) {
        std::vector<int16_t> result;
        if (!samples || inRate <= 0 || outRate <= 0 || channelCount <= 0) return result;
        count -= count % channelCount;
        if (inRate == outRate) { reset(); result.assign(samples, samples + count); return result; }
        if (inputRate != inRate || outputRate != outRate || channels != channelCount) {
            reset(); inputRate = inRate; outputRate = outRate; channels = channelCount;
        }
        pending.insert(pending.end(), samples, samples + count);
        const size_t frames = pending.size() / channels;
        const double step = double(inRate) / outRate;
        while (position + 1 < frames) {
            const size_t index = static_cast<size_t>(position);
            const double fraction = position - index;
            for (int channel = 0; channel < channels; ++channel) {
                const int a = pending[index * channels + channel];
                const int b = pending[(index + 1) * channels + channel];
                result.push_back(static_cast<int16_t>(a + (b - a) * fraction));
            }
            position += step;
        }
        const size_t consumed = frames ? std::min(static_cast<size_t>(position), frames - 1) : 0;
        pending.erase(pending.begin(), pending.begin() + consumed * channels);
        position -= consumed;
        return result;
    }
};
}
