#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace oceanblast {
// Streaming interpolation keeps the phase and boundary frame across DMA buffers.
class PcmResampler {
    std::vector<int16_t> pending;
    size_t readIndex = 0;
    double position = 0;
    int inputRate = 0, outputRate = 0, channels = 0;
public:
    void reset() { pending.clear(); readIndex = 0; position = 0; inputRate = outputRate = channels = 0; }
    void processInto(const int16_t* samples, size_t count, int inRate, int outRate, int channelCount,
                     std::vector<int16_t>& result) {
        result.clear();
        if (!samples || inRate <= 0 || outRate <= 0 || channelCount <= 0) return;
        count -= count % channelCount;
        if (inRate == outRate) { reset(); result.assign(samples, samples + count); return; }
        if (inputRate != inRate || outputRate != outRate || channels != channelCount) {
            reset(); inputRate = inRate; outputRate = outRate; channels = channelCount;
        }
        pending.insert(pending.end(), samples, samples + count);
        const size_t availableSamples = pending.size() - readIndex;
        const size_t frames = availableSamples / channels;
        const double step = double(inRate) / outRate;
        const size_t estimatedFrames = frames > 1 ? static_cast<size_t>((frames - 1) / step) + 2 : 0;
        if (result.capacity() < estimatedFrames * static_cast<size_t>(channels)) {
            result.reserve(estimatedFrames * static_cast<size_t>(channels));
        }
        const int16_t* base = pending.data() + readIndex;
        while (position + 1 < frames) {
            const size_t index = static_cast<size_t>(position);
            const double fraction = position - index;
            for (int channel = 0; channel < channels; ++channel) {
                const int a = base[index * channels + channel];
                const int b = base[(index + 1) * channels + channel];
                result.push_back(static_cast<int16_t>(a + (b - a) * fraction));
            }
            position += step;
        }
        const size_t consumed = frames ? std::min(static_cast<size_t>(position), frames - 1) : 0;
        readIndex += consumed * channels;
        position -= consumed;
        if (readIndex >= 2048 || readIndex * 2 >= pending.size()) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(readIndex));
            readIndex = 0;
        }
    }
    std::vector<int16_t> process(const int16_t* samples, size_t count, int inRate, int outRate, int channelCount) {
        std::vector<int16_t> result;
        processInto(samples, count, inRate, outRate, channelCount, result);
        return result;
    }
};
}
