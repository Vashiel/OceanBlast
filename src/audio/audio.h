#ifndef OCEANBLAST_AUDIO_H
#define OCEANBLAST_AUDIO_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "resampler.h"

namespace oceanblast {

class Audio {
public:
    Audio();
    ~Audio();

    bool init(int sampleRate = 22050, int channels = 2);
    void writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate = 0);
    void close();

    bool isInitialized() const { return m_initialized.load(std::memory_order_acquire); }
    void setVolume(unsigned volumePercent) { m_volume.store(volumePercent > 100 ? 100 : volumePercent, std::memory_order_relaxed); }
    unsigned getVolume() const { return m_volume.load(std::memory_order_relaxed); }
    int getSampleRate() const { return m_sampleRate; }
    int getChannels() const { return m_channels; }
    uint64_t getDroppedSamples() const { return m_droppedSamples.load(std::memory_order_relaxed); }
    uint64_t getQueuedFrames() const;
    bool getPlayedFrames(uint64_t& frames) const;
    size_t getPendingFrames() const;
    uint64_t getSubmittedFrames() const { return m_submittedFrames.load(std::memory_order_relaxed); }
    uint64_t getEmptyQueueEvents() const { return m_emptyQueueEvents.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_initialized{false};
    void* m_hWaveOut;
    int m_sampleRate;
    int m_channels;
    std::atomic<unsigned> m_volume{100};

    static constexpr size_t NUM_BUFFERS = 16;
    static constexpr size_t BUFFER_BYTES = 4096;

    struct AudioBuffer {
        void* header;
        std::vector<uint8_t> data;
    };

    std::vector<AudioBuffer> m_buffers;
    size_t m_currentBuffer;
    std::vector<int16_t> m_resampleBuffer;
    std::vector<int16_t> m_pendingSamples;
    PcmResampler m_resampler;
    std::atomic<uint64_t> m_droppedSamples{0};
    std::atomic<uint64_t> m_submittedFrames{0};
    std::atomic<uint64_t> m_emptyQueueEvents{0};
    bool m_queueEmptyReported = false;
    bool m_preroll = false;
    mutable uint32_t m_positionType = 0, m_lastPosition = 0;
    mutable uint64_t m_positionWrap = 0;
    void observeQueue();
};

} // namespace oceanblast

#endif // OCEANBLAST_AUDIO_H
