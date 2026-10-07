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

    bool init(int sampleRate = 44100, int channels = 2);
    void writeSamples(const int16_t* samples, size_t sampleCount);
    void close();

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized;
    void* m_hWaveOut;

    static constexpr size_t NUM_BUFFERS = 8;
    static constexpr size_t BUFFER_BYTES = 4096;

    struct AudioBuffer {
        void* header;
        std::vector<uint8_t> data;
    };

    std::vector<AudioBuffer> m_buffers;
    size_t m_currentBuffer;
};

} // namespace oceanblast

#endif // OCEANBLAST_AUDIO_H
