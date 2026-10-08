#include "audio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <cstring>

namespace oceanblast {

Audio::Audio()
    : m_initialized(false), m_hWaveOut(nullptr), m_sampleRate(22050), m_channels(2), m_currentBuffer(0) {
}

Audio::~Audio() {
    close();
}

bool Audio::init(int sampleRate, int channels) {
    if (sampleRate < 4000 || sampleRate > 192000 || channels < 1 || channels > 2) return false;
    if (m_initialized) {
        close();
    }

    m_sampleRate = sampleRate;
    m_channels = channels;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(channels);
    wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    HWAVEOUT hWave = nullptr;
    MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        // Fallback to 44100 Hz if device does not support native rate
        if (sampleRate != 44100) {
            std::cerr << "[Audio] waveOutOpen failed at " << sampleRate << " Hz; falling back to 44100 Hz." << std::endl;
            wfx.nSamplesPerSec = 44100;
            wfx.nAvgBytesPerSec = 44100 * wfx.nBlockAlign;
            res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
            if (res == MMSYSERR_NOERROR) {
                m_sampleRate = 44100;
            }
        }
        if (res != MMSYSERR_NOERROR) {
            std::cerr << "[Audio] Failed to open waveOut device (error " << res << ")." << std::endl;
            return false;
        }
    }

    m_hWaveOut = static_cast<void*>(hWave);

    m_buffers.resize(NUM_BUFFERS);
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        m_buffers[i].data.resize(BUFFER_BYTES, 0);
        WAVEHDR* hdr = new WAVEHDR();
        std::memset(hdr, 0, sizeof(WAVEHDR));
        hdr->lpData = reinterpret_cast<LPSTR>(m_buffers[i].data.data());
        hdr->dwBufferLength = static_cast<DWORD>(BUFFER_BYTES);
        hdr->dwFlags = 0;
        m_buffers[i].header = static_cast<void*>(hdr);
    }

    m_currentBuffer = 0;
    m_droppedSamples = 0;
    m_resampler.reset();
    m_pendingSamples.clear();
    m_initialized = true;

    std::cout << "[Audio] Win32 waveOut initialized (" << m_sampleRate << " Hz, "
              << m_channels << " channels, 16-bit PCM, " << NUM_BUFFERS << " buffers)." << std::endl;
    return true;
}

void Audio::writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate) {
    if (!m_initialized || !m_hWaveOut || !samples || sampleCount == 0) return;
    sampleCount -= sampleCount % m_channels;

    if (inputSampleRate <= 0) {
        inputSampleRate = m_sampleRate;
    }

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
    HWAVEOUT hWave = static_cast<HWAVEOUT>(m_hWaveOut);
    m_pendingSamples.insert(m_pendingSamples.end(), playSamples, playSamples + playSampleCount);
    playSamples = m_pendingSamples.data();
    size_t byteCount = m_pendingSamples.size() * sizeof(int16_t);
    size_t offset = 0;

    while (offset + BUFFER_BYTES <= byteCount) {
        AudioBuffer* targetBuf = nullptr;
        WAVEHDR* targetHdr = nullptr;

        // Find an available buffer starting from m_currentBuffer
        for (size_t k = 0; k < NUM_BUFFERS; ++k) {
            size_t idx = (m_currentBuffer + k) % NUM_BUFFERS;
            WAVEHDR* hdr = static_cast<WAVEHDR*>(m_buffers[idx].header);
            if (!(hdr->dwFlags & WHDR_PREPARED) || (hdr->dwFlags & WHDR_DONE)) {
                m_currentBuffer = idx;
                targetBuf = &m_buffers[idx];
                targetHdr = hdr;
                break;
            }
        }

        // If all buffers are currently busy in waveOut, do NOT block the CPU thread with Sleep().
        // Blocking the emulation thread causes severe stuttering and cuts MIPS performance.
        if (!targetBuf || !targetHdr) {
            m_droppedSamples += (byteCount - offset) / sizeof(int16_t);
            m_pendingSamples.clear();
            return;
        }

        if (targetHdr->dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(hWave, targetHdr, sizeof(WAVEHDR));
        }

        size_t chunkSize = std::min(byteCount - offset, static_cast<size_t>(BUFFER_BYTES));
        std::memcpy(targetBuf->data.data(), reinterpret_cast<const uint8_t*>(playSamples) + offset, chunkSize);
        if (chunkSize < BUFFER_BYTES) {
            std::memset(targetBuf->data.data() + chunkSize, 0, BUFFER_BYTES - chunkSize);
        }

        targetHdr->dwBufferLength = static_cast<DWORD>(chunkSize);
        targetHdr->dwFlags = 0;

        if (waveOutPrepareHeader(hWave, targetHdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            if (waveOutWrite(hWave, targetHdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                m_droppedSamples += chunkSize / sizeof(int16_t);
        } else {
            m_droppedSamples += chunkSize / sizeof(int16_t);
        }

        offset += chunkSize;
        m_currentBuffer = (m_currentBuffer + 1) % NUM_BUFFERS;
    }
    m_pendingSamples.erase(m_pendingSamples.begin(), m_pendingSamples.begin() + offset / sizeof(int16_t));
}

void Audio::close() {
    if (!m_initialized) return;

    if (m_hWaveOut) {
        HWAVEOUT hWave = static_cast<HWAVEOUT>(m_hWaveOut);
        waveOutReset(hWave);

        for (auto& buf : m_buffers) {
            if (buf.header) {
                WAVEHDR* hdr = static_cast<WAVEHDR*>(buf.header);
                if (hdr->dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(hWave, hdr, sizeof(WAVEHDR));
                }
                delete hdr;
                buf.header = nullptr;
            }
        }

        waveOutClose(hWave);
        m_hWaveOut = nullptr;
    }

    m_buffers.clear();
    m_initialized = false;
    std::cout << "[Audio] Sound subsystem closed." << std::endl;
}

} // namespace oceanblast

#else // Non-Windows fallback

namespace oceanblast {
Audio::Audio() : m_initialized(false), m_hWaveOut(nullptr), m_sampleRate(22050), m_channels(2), m_currentBuffer(0) {}
Audio::~Audio() {}
bool Audio::init(int, int) { return false; }
void Audio::writeSamples(const int16_t*, size_t, int) {}
void Audio::close() {}
}

#endif
