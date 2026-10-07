#ifndef OCEANBLAST_AUDIO_H
#define OCEANBLAST_AUDIO_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace oceanblast {

class Audio {
public:
    Audio();
    ~Audio();

    bool init(int sampleRate = 22050, int channels = 2);
    void writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate = 0);
    void close();

    bool isInitialized() const { return m_initialized; }
    int getSampleRate() const { return m_sampleRate; }
    int getChannels() const { return m_channels; }

private:
    bool m_initialized;
    void* m_hWaveOut;
    int m_sampleRate;
    int m_channels;

    static constexpr size_t NUM_BUFFERS = 16;
    static constexpr size_t BUFFER_BYTES = 4096;

    struct AudioBuffer {
        void* header;
        std::vector<uint8_t> data;
    };

    std::vector<AudioBuffer> m_buffers;
    size_t m_currentBuffer;
    std::vector<int16_t> m_resampleBuffer;
};

} // namespace oceanblast

#endif // OCEANBLAST_AUDIO_H
