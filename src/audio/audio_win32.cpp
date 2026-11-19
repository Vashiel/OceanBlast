#include "audio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace oceanblast {

namespace {

struct Win32AudioState {
    HWAVEOUT hWave = nullptr;
    HANDLE doneEvent = nullptr;
    HANDLE wakeEvent = nullptr;
    std::thread worker;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> deviceFailed{false};

    std::mutex ringMutex;
    std::vector<int16_t> ring;
    size_t ringRead = 0;
    size_t ringWrite = 0;
    size_t ringCount = 0;
    std::atomic<size_t> ringPendingSamples{0};

    std::vector<bool> inFlight;
    std::atomic<uint64_t> queuedFrames{0};
    std::atomic<uint64_t> playedFrames{0};
    std::atomic<bool> playedPositionValid{false};
    uint64_t waveSubmittedFrames = 0;
    bool preroll = false;
    bool queueEmptyReported = false;
    uint32_t positionType = 0;
    uint32_t lastPosition = 0;
    uint64_t positionWrap = 0;

    bool pollPosition(int sampleRate, int channels) {
        if (!hWave || deviceFailed.load(std::memory_order_relaxed)) {
            playedPositionValid.store(false, std::memory_order_release);
            return false;
        }
        MMTIME position{};
        position.wType = TIME_SAMPLES;
        if (waveOutGetPosition(hWave, &position, sizeof(position)) != MMSYSERR_NOERROR) {
            playedPositionValid.store(false, std::memory_order_release);
            return false;
        }
        uint32_t raw = 0;
        if (position.wType == TIME_SAMPLES) raw = position.u.sample;
        else if (position.wType == TIME_BYTES) raw = position.u.cb;
        else if (position.wType == TIME_MS) raw = position.u.ms;
        else {
            playedPositionValid.store(false, std::memory_order_release);
            return false;
        }
        if (positionType && positionType != position.wType) {
            playedPositionValid.store(false, std::memory_order_release);
            return false;
        }
        positionType = position.wType;
        if (raw < lastPosition) {
            if (lastPosition - raw < 0x80000000u) {
                playedPositionValid.store(false, std::memory_order_release);
                return false;
            }
            positionWrap += uint64_t(1) << 32;
        }
        lastPosition = raw;
        const uint64_t total = positionWrap + raw;
        const uint64_t frames = position.wType == TIME_SAMPLES
            ? total
            : (position.wType == TIME_BYTES
                ? total / (sizeof(int16_t) * static_cast<size_t>(channels))
                : total * static_cast<uint64_t>(sampleRate) / 1000);
        playedFrames.store(frames, std::memory_order_release);
        playedPositionValid.store(true, std::memory_order_release);
        return true;
    }
};

} // namespace

Audio::Audio()
    : m_initialized(false), m_hWaveOut(nullptr), m_sampleRate(22050), m_channels(2), m_currentBuffer(0) {
}

Audio::~Audio() {
    close();
}

bool Audio::init(int sampleRate, int channels) {
    if (sampleRate < 4000 || sampleRate > 192000 || channels < 1 || channels > 2) return false;
    if (m_initialized.load(std::memory_order_acquire)) {
        close();
    }

    m_sampleRate = sampleRate;
    m_channels = channels;

    auto* state = new Win32AudioState();
    state->doneEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state->doneEvent || !state->wakeEvent) {
        if (state->doneEvent) CloseHandle(state->doneEvent);
        if (state->wakeEvent) CloseHandle(state->wakeEvent);
        delete state;
        return false;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(channels);
    wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    HWAVEOUT hWave = nullptr;
    MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx,
                               reinterpret_cast<DWORD_PTR>(state->doneEvent), 0, CALLBACK_EVENT);
    if (res != MMSYSERR_NOERROR) {
        // Fallback to 44100 Hz if device does not support native rate
        if (sampleRate != 44100) {
            std::cerr << "[Audio] waveOutOpen failed at " << sampleRate << " Hz; falling back to 44100 Hz." << std::endl;
            wfx.nSamplesPerSec = 44100;
            wfx.nAvgBytesPerSec = 44100 * wfx.nBlockAlign;
            res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx,
                              reinterpret_cast<DWORD_PTR>(state->doneEvent), 0, CALLBACK_EVENT);
            if (res == MMSYSERR_NOERROR) {
                m_sampleRate = 44100;
            }
        }
        if (res != MMSYSERR_NOERROR) {
            std::cerr << "[Audio] Failed to open waveOut device (error " << res << ")." << std::endl;
            CloseHandle(state->doneEvent);
            CloseHandle(state->wakeEvent);
            delete state;
            return false;
        }
    }

    state->hWave = hWave;
    state->preroll = waveOutPause(hWave) == MMSYSERR_NOERROR;
    state->inFlight.assign(NUM_BUFFERS, false);
    state->ring.resize(NUM_BUFFERS * (BUFFER_BYTES / sizeof(int16_t)), 0);

    m_buffers.resize(NUM_BUFFERS);
    bool prepareOk = true;
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        m_buffers[i].data.assign(BUFFER_BYTES, 0);
        WAVEHDR* hdr = new WAVEHDR();
        std::memset(hdr, 0, sizeof(WAVEHDR));
        hdr->lpData = reinterpret_cast<LPSTR>(m_buffers[i].data.data());
        hdr->dwBufferLength = static_cast<DWORD>(BUFFER_BYTES);
        hdr->dwFlags = 0;
        if (waveOutPrepareHeader(hWave, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            delete hdr;
            m_buffers[i].header = nullptr;
            prepareOk = false;
            break;
        }
        m_buffers[i].header = static_cast<void*>(hdr);
    }

    if (!prepareOk) {
        std::cerr << "[Audio] Failed to prepare waveOut buffers." << std::endl;
        for (auto& buf : m_buffers) {
            if (buf.header) {
                auto* hdr = static_cast<WAVEHDR*>(buf.header);
                if (hdr->dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(hWave, hdr, sizeof(WAVEHDR));
                }
                delete hdr;
                buf.header = nullptr;
            }
        }
        m_buffers.clear();
        waveOutClose(hWave);
        CloseHandle(state->doneEvent);
        CloseHandle(state->wakeEvent);
        delete state;
        return false;
    }

    m_currentBuffer = 0;
    m_producedFrames.store(0, std::memory_order_relaxed);
    m_droppedSamples.store(0, std::memory_order_relaxed);
    m_submittedFrames.store(0, std::memory_order_relaxed);
    m_emptyQueueEvents.store(0, std::memory_order_relaxed);
    m_positionType = m_lastPosition = 0;
    m_positionWrap = 0;
    m_queueEmptyReported = false;
    m_preroll = state->preroll;
    m_resampler.reset();
    m_resampleBuffer.clear();
    m_pendingSamples.clear();
    state->pollPosition(m_sampleRate, m_channels);

    m_hWaveOut = static_cast<void*>(state);
    m_initialized.store(true, std::memory_order_release);

    state->worker = std::thread([this, state]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        const size_t chunkSamples = BUFFER_BYTES / sizeof(int16_t);
        const size_t chunkFrames = chunkSamples / static_cast<size_t>(m_channels);
        const size_t prerollFrames = 2 * chunkFrames;
        HANDLE events[2] = { state->wakeEvent, state->doneEvent };
        size_t nextBuffer = 0;

        while (!state->stopRequested.load(std::memory_order_acquire)) {
            // Reclaim finished buffers and count frames still queued in waveOut.
            uint64_t queued = 0;
            for (size_t i = 0; i < NUM_BUFFERS; ++i) {
                auto* hdr = static_cast<WAVEHDR*>(m_buffers[i].header);
                if (!hdr) continue;
                if (state->inFlight[i]) {
                    if (hdr->dwFlags & WHDR_DONE) {
                        state->inFlight[i] = false;
                    } else {
                        queued += hdr->dwBufferLength / (sizeof(int16_t) * static_cast<size_t>(m_channels));
                    }
                }
            }

            if (state->waveSubmittedFrames > 0 && !state->preroll) {
                if (queued == 0) {
                    if (!state->queueEmptyReported) {
                        m_emptyQueueEvents.fetch_add(1, std::memory_order_relaxed);
                    }
                    state->queueEmptyReported = true;
                } else {
                    state->queueEmptyReported = false;
                }
            }

            // Submit as many full buffers as are available in both the ring and waveOut pool.
            bool submittedAny = false;
            while (!state->stopRequested.load(std::memory_order_acquire) &&
                   !state->deviceFailed.load(std::memory_order_relaxed)) {
                size_t chosen = NUM_BUFFERS;
                for (size_t k = 0; k < NUM_BUFFERS; ++k) {
                    const size_t idx = (nextBuffer + k) % NUM_BUFFERS;
                    auto* hdr = static_cast<WAVEHDR*>(m_buffers[idx].header);
                    if (hdr && !state->inFlight[idx]) {
                        chosen = idx;
                        break;
                    }
                }
                if (chosen == NUM_BUFFERS) break;

                auto* outSamples = reinterpret_cast<int16_t*>(m_buffers[chosen].data.data());
                bool gotChunk = false;
                {
                    std::lock_guard<std::mutex> lock(state->ringMutex);
                    if (state->ringCount >= chunkSamples) {
                        const size_t firstPart = std::min(chunkSamples, state->ring.size() - state->ringRead);
                        std::memcpy(outSamples, state->ring.data() + state->ringRead, firstPart * sizeof(int16_t));
                        if (firstPart < chunkSamples) {
                            std::memcpy(outSamples + firstPart, state->ring.data(),
                                        (chunkSamples - firstPart) * sizeof(int16_t));
                        }
                        state->ringRead = (state->ringRead + chunkSamples) % state->ring.size();
                        state->ringCount -= chunkSamples;
                        state->ringPendingSamples.store(state->ringCount, std::memory_order_relaxed);
                        gotChunk = true;
                    }
                }
                if (!gotChunk) break;

                const unsigned vol = m_volume.load(std::memory_order_relaxed);
                if (vol < 100) {
                    const int v = static_cast<int>(vol);
                    for (size_t s = 0; s < chunkSamples; ++s) {
                        outSamples[s] = static_cast<int16_t>((static_cast<int>(outSamples[s]) * v) / 100);
                    }
                }

                auto* hdr = static_cast<WAVEHDR*>(m_buffers[chosen].header);
                hdr->dwBufferLength = static_cast<DWORD>(BUFFER_BYTES);
                if (waveOutWrite(state->hWave, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                    state->inFlight[chosen] = true;
                    state->waveSubmittedFrames += chunkFrames;
                    m_submittedFrames.store(state->waveSubmittedFrames, std::memory_order_relaxed);
                    queued += chunkFrames;
                    state->queueEmptyReported = false;
                    submittedAny = true;
                    if (state->preroll && state->waveSubmittedFrames >= prerollFrames) {
                        if (waveOutRestart(state->hWave) != MMSYSERR_NOERROR) {
                            std::cerr << "[Audio] Failed to start buffered output." << std::endl;
                            state->deviceFailed.store(true, std::memory_order_release);
                            break;
                        }
                        state->preroll = false;
                    }
                } else {
                    m_droppedSamples.fetch_add(chunkSamples, std::memory_order_relaxed);
                }

                nextBuffer = (chosen + 1) % NUM_BUFFERS;
            }

            state->queuedFrames.store(queued, std::memory_order_relaxed);
            state->pollPosition(m_sampleRate, m_channels);

            if (!submittedAny) {
                const DWORD timeoutMs = queued > 0 ? 8u : 25u;
                WaitForMultipleObjects(2, events, FALSE, timeoutMs);
            }
        }
    });

    std::cout << "[Audio] Win32 waveOut initialized (" << m_sampleRate << " Hz, "
              << m_channels << " channels, 16-bit PCM, " << NUM_BUFFERS << " buffers, async)." << std::endl;
    return true;
}

uint64_t Audio::getQueuedFrames() const {
    auto* state = static_cast<Win32AudioState*>(m_hWaveOut);
    if (!m_initialized.load(std::memory_order_acquire) || !state) return 0;
    return state->queuedFrames.load(std::memory_order_relaxed);
}

bool Audio::getPlayedFrames(uint64_t& frames) const {
    auto* state = static_cast<Win32AudioState*>(m_hWaveOut);
    if (!m_initialized.load(std::memory_order_acquire) || !state) return false;
    if (!state->playedPositionValid.load(std::memory_order_acquire)) return false;
    frames = state->playedFrames.load(std::memory_order_acquire);
    return true;
}

size_t Audio::getPendingFrames() const {
    auto* state = static_cast<Win32AudioState*>(m_hWaveOut);
    if (!m_initialized.load(std::memory_order_acquire) || !state) return 0;
    return state->ringPendingSamples.load(std::memory_order_relaxed) / static_cast<size_t>(m_channels);
}

void Audio::observeQueue() {
    // Observed asynchronously on the Win32 audio worker thread.
}

void Audio::writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate) {
    auto* state = static_cast<Win32AudioState*>(m_hWaveOut);
    if (!m_initialized.load(std::memory_order_acquire) || !state || !samples || sampleCount == 0) return;
    if (state->deviceFailed.load(std::memory_order_relaxed)) return;

    sampleCount -= sampleCount % static_cast<size_t>(m_channels);
    if (sampleCount == 0) return;
    if (inputSampleRate <= 0) {
        inputSampleRate = m_sampleRate;
    }

    const int16_t* playSamples = samples;
    size_t playSampleCount = sampleCount;

    if (inputSampleRate != m_sampleRate) {
        m_resampler.processInto(samples, sampleCount, inputSampleRate, m_sampleRate, m_channels, m_resampleBuffer);
        playSamples = m_resampleBuffer.data();
        playSampleCount = m_resampleBuffer.size();
    } else {
        m_resampler.reset();
    }

    if (playSampleCount == 0) return;

    const size_t chunkSamples = BUFFER_BYTES / sizeof(int16_t);
    m_producedFrames.fetch_add(playSampleCount / static_cast<size_t>(m_channels), std::memory_order_relaxed);
    bool shouldWake = false;

    {
        std::lock_guard<std::mutex> lock(state->ringMutex);
        const size_t capacity = state->ring.size();
        const size_t freeSamples = capacity - state->ringCount;
        // Align free capacity to whole frames so stereo channels never desynchronize.
        const size_t maxAccept = freeSamples - (freeSamples % static_cast<size_t>(m_channels));
        const size_t accept = std::min(playSampleCount, maxAccept);
        if (accept < playSampleCount) {
            m_droppedSamples.fetch_add(playSampleCount - accept, std::memory_order_relaxed);
        }
        if (accept > 0) {
            const size_t firstPart = std::min(accept, capacity - state->ringWrite);
            std::memcpy(state->ring.data() + state->ringWrite, playSamples, firstPart * sizeof(int16_t));
            if (firstPart < accept) {
                std::memcpy(state->ring.data(), playSamples + firstPart, (accept - firstPart) * sizeof(int16_t));
            }
            state->ringWrite = (state->ringWrite + accept) % capacity;
            state->ringCount += accept;
            state->ringPendingSamples.store(state->ringCount, std::memory_order_relaxed);

            shouldWake = state->ringCount >= chunkSamples;
        }
    }

    if (shouldWake) {
        SetEvent(state->wakeEvent);
    }
}

void Audio::close() {
    if (!m_initialized.exchange(false, std::memory_order_acq_rel)) return;

    auto* state = static_cast<Win32AudioState*>(m_hWaveOut);
    m_hWaveOut = nullptr;

    if (state) {
        state->stopRequested.store(true, std::memory_order_release);
        if (state->wakeEvent) SetEvent(state->wakeEvent);
        if (state->worker.joinable()) state->worker.join();

        if (state->hWave) {
            waveOutReset(state->hWave);
            for (auto& buf : m_buffers) {
                if (buf.header) {
                    auto* hdr = static_cast<WAVEHDR*>(buf.header);
                    if (hdr->dwFlags & WHDR_PREPARED) {
                        waveOutUnprepareHeader(state->hWave, hdr, sizeof(WAVEHDR));
                    }
                    delete hdr;
                    buf.header = nullptr;
                }
            }
            waveOutClose(state->hWave);
            state->hWave = nullptr;
        }
        if (state->wakeEvent) CloseHandle(state->wakeEvent);
        if (state->doneEvent) CloseHandle(state->doneEvent);
        delete state;
    }

    m_buffers.clear();
    m_resampleBuffer.clear();
    m_pendingSamples.clear();
    m_resampler.reset();
    std::cout << "[Audio] Sound subsystem closed." << std::endl;
}

} // namespace oceanblast

#else // Non-Windows fallback

namespace oceanblast {
Audio::Audio() : m_initialized(false), m_hWaveOut(nullptr), m_sampleRate(22050), m_channels(2), m_currentBuffer(0) {}
Audio::~Audio() {}
bool Audio::init(int, int) { return false; }
uint64_t Audio::getQueuedFrames() const { return 0; }
bool Audio::getPlayedFrames(uint64_t&) const { return false; }
size_t Audio::getPendingFrames() const { return 0; }
void Audio::observeQueue() {}
void Audio::writeSamples(const int16_t*, size_t, int) {}
void Audio::close() {}
} // namespace oceanblast

#endif
