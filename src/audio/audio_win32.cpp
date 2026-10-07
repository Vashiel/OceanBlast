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
    m_initialized = true;

    std::cout << "[Audio] Win32 waveOut initialized (" << m_sampleRate << " Hz, "
              << m_channels << " channels, 16-bit PCM, " << NUM_BUFFERS << " buffers)." << std::endl;
    return true;
}

void Audio::writeSamples(const int16_t* samples, size_t sampleCount, int inputSampleRate) {
    if (!m_initialized || !m_hWaveOut || !samples || sampleCount == 0) return;

    if (inputSampleRate <= 0) {
        inputSampleRate = m_sampleRate;
    }

    const int16_t* playSamples = samples;
    size_t playSampleCount = sampleCount;

    // High-fidelity resampling if input rate does not match waveOut device rate
    if (inputSampleRate != m_sampleRate && m_channels == 2) {
        if (inputSampleRate == 22050 && m_sampleRate == 44100) {
            // 2x linear interpolation (22050 Hz stereo -> 44100 Hz stereo)
            size_t frameCount = sampleCount / 2;
            m_resampleBuffer.resize(frameCount * 4);
            for (size_t i = 0; i < frameCount; ++i) {
                int16_t l0 = samples[i * 2 + 0];
                int16_t r0 = samples[i * 2 + 1];
                int16_t l1 = (i + 1 < frameCount) ? samples[(i + 1) * 2 + 0] : l0;
                int16_t r1 = (i + 1 < frameCount) ? samples[(i + 1) * 2 + 1] : r0;

                // Frame 0: original sample
                m_resampleBuffer[i * 4 + 0] = l0;
                m_resampleBuffer[i * 4 + 1] = r0;
                // Frame 1: interpolated midpoint
                m_resampleBuffer[i * 4 + 2] = static_cast<int16_t>((static_cast<int32_t>(l0) + l1) / 2);
                m_resampleBuffer[i * 4 + 3] = static_cast<int16_t>((static_cast<int32_t>(r0) + r1) / 2);
            }
            playSamples = m_resampleBuffer.data();
            playSampleCount = m_resampleBuffer.size();
        } else if (inputSampleRate == 44100 && m_sampleRate == 22050) {
            // 2x decimation (44100 Hz stereo -> 22050 Hz stereo)
            size_t frameCount = sampleCount / 4;
            m_resampleBuffer.resize(frameCount * 2);
            for (size_t i = 0; i < frameCount; ++i) {
                m_resampleBuffer[i * 2 + 0] = samples[i * 4 + 0];
                m_resampleBuffer[i * 2 + 1] = samples[i * 4 + 1];
            }
            playSamples = m_resampleBuffer.data();
            playSampleCount = m_resampleBuffer.size();
        }
    }

    HWAVEOUT hWave = static_cast<HWAVEOUT>(m_hWaveOut);
    size_t byteCount = playSampleCount * sizeof(int16_t);
    size_t offset = 0;

    while (offset < byteCount) {
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
            waveOutWrite(hWave, targetHdr, sizeof(WAVEHDR));
        }

        offset += chunkSize;
        m_currentBuffer = (m_currentBuffer + 1) % NUM_BUFFERS;
    }
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
Audio::Audio() : m_initialized(false), m_hWaveOut(nullptr), m_currentBuffer(0) {}
Audio::~Audio() {}
bool Audio::init(int, int) { return false; }
void Audio::writeSamples(const int16_t*, size_t) {}
void Audio::close() {}
}

#endif
