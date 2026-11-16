#include "audio.h"

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace oceanblast {

namespace {

struct SdlAudioState {
    SDL_AudioDeviceID deviceId = 0;
    std::vector<int16_t> scaledScratch;
};

constexpr size_t kPrerollFrames = 1024;
constexpr size_t kMaxQueuedFrames = 12288;

} // namespace

Audio::Audio()
    : m_initialized(false), m_hWaveOut(nullptr), m_sampleRate(22050),
      m_channels(2), m_currentBuffer(0) {}

Audio::~Audio() {
    close();
}

bool Audio::init(int sampleRate, int channels) {
    close();

    if (sampleRate < 4000 || sampleRate > 192000 || (channels != 1 && channels != 2)) {
        std::cerr << "[Audio] Unsupported SDL2 audio format: " << sampleRate << " Hz, " << channels << " ch.\n";
        return false;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "[Audio] SDL2 audio subsystem unavailable: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq = sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(channels);
    desired.samples = 512;

    SDL_AudioSpec obtained{};
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (dev == 0) {
        std::cerr << "[Audio] Failed to open SDL2 audio device: " << SDL_GetError() << '\n';
        return false;
    }

    auto* state = new SdlAudioState();
    state->deviceId = dev;

    m_hWaveOut = state;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
    m_preroll = true;
    m_queueEmptyReported = false;
    m_droppedSamples = 0;
    m_submittedFrames = 0;
    m_emptyQueueEvents = 0;
    m_pendingSamples.clear();
    m_resampler.reset();

    std::cout << "[Audio] Initialized SDL2 audio (" << m_sampleRate << " Hz, "
              << m_channels << " ch, 16-bit)." << std::endl;
    return true;
}

void Audio::observeQueue() {
    auto* state = static_cast<SdlAudioState*>(m_hWaveOut);
    if (!m_initialized || !state || !state->deviceId || m_preroll) return;
    if (SDL_GetQueuedAudioSize(state->deviceId) == 0) {
        if (!m_queueEmptyReported) ++m_emptyQueueEvents;
        m_queueEmptyReported = true;
    } else {
        m_queueEmptyReported = false;
    }
}

void Audio::writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate) {
    auto* state = static_cast<SdlAudioState*>(m_hWaveOut);
    if (!m_initialized || !state || !state->deviceId || !samples || sampleCount == 0) return;

    observeQueue();
    sampleCount -= sampleCount % static_cast<size_t>(m_channels);
    if (inputSampleRate <= 0) inputSampleRate = m_sampleRate;

    const int16_t* playSamples = samples;
    size_t playSampleCount = sampleCount;
    if (inputSampleRate != m_sampleRate) {
        m_resampleBuffer = m_resampler.process(samples, sampleCount, inputSampleRate, m_sampleRate, m_channels);
        playSamples = m_resampleBuffer.data();
        playSampleCount = m_resampleBuffer.size();
    } else {
        m_resampler.reset();
    }
    if (playSampleCount == 0) return;

    const size_t bytesPerFrame = static_cast<size_t>(m_channels) * sizeof(int16_t);
    const size_t queuedFrames = SDL_GetQueuedAudioSize(state->deviceId) / bytesPerFrame;
    if (queuedFrames >= kMaxQueuedFrames) {
        m_droppedSamples += playSampleCount;
        return;
    }

    const int16_t* queuePtr = playSamples;
    if (m_volume < 100) {
        state->scaledScratch.resize(playSampleCount);
        const int vol = static_cast<int>(m_volume);
        for (size_t i = 0; i < playSampleCount; ++i) {
            state->scaledScratch[i] = static_cast<int16_t>((static_cast<int>(playSamples[i]) * vol) / 100);
        }
        queuePtr = state->scaledScratch.data();
    }

    const Uint32 bytes = static_cast<Uint32>(playSampleCount * sizeof(int16_t));
    if (SDL_QueueAudio(state->deviceId, queuePtr, bytes) != 0) {
        m_droppedSamples += playSampleCount;
        return;
    }

    m_submittedFrames += playSampleCount / static_cast<size_t>(m_channels);
    m_queueEmptyReported = false;
    if (m_preroll && m_submittedFrames >= kPrerollFrames) {
        SDL_PauseAudioDevice(state->deviceId, 0);
        m_preroll = false;
    }
}

uint64_t Audio::getQueuedFrames() const {
    auto* state = static_cast<SdlAudioState*>(m_hWaveOut);
    if (!m_initialized || !state || !state->deviceId) return 0;
    return SDL_GetQueuedAudioSize(state->deviceId) / (static_cast<size_t>(m_channels) * sizeof(int16_t));
}

bool Audio::getPlayedFrames(uint64_t& playedFrames) const {
    if (!m_initialized) return false;
    const uint64_t queued = getQueuedFrames();
    playedFrames = m_submittedFrames > queued ? m_submittedFrames - queued : 0;
    return true;
}

void Audio::close() {
    auto* state = static_cast<SdlAudioState*>(m_hWaveOut);
    m_hWaveOut = nullptr;
    m_initialized = false;
    m_pendingSamples.clear();
    m_resampleBuffer.clear();
    m_resampler.reset();
    if (state) {
        if (state->deviceId) {
            SDL_ClearQueuedAudio(state->deviceId);
            SDL_CloseAudioDevice(state->deviceId);
        }
        delete state;
    }
    if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

} // namespace oceanblast
