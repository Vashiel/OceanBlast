#include "audio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <cstring>

namespace oceanblast {

Audio::Audio()
    : m_initialized(false), m_hWaveOut(nullptr), m_currentBuffer(0) {
}

Audio::~Audio() {
    close();
}

bool Audio::init(int sampleRate, int channels) {
    if (m_initialized) {
        close();
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
    MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        std::cerr << "[Audio] Failed to open waveOut device (error " << res << ")." << std::endl;
        return false;
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

    std::cout << "[Audio] Win32 waveOut initialized (" << sampleRate << " Hz, "
              << channels << " channels, 16-bit PCM, " << NUM_BUFFERS << " buffers)." << std::endl;
    return true;
}

void Audio::writeSamples(const int16_t* samples, size_t sampleCount) {
    if (!m_initialized || !m_hWaveOut || !samples || sampleCount == 0) return;

    HWAVEOUT hWave = static_cast<HWAVEOUT>(m_hWaveOut);
    size_t byteCount = sampleCount * sizeof(int16_t);
    size_t offset = 0;

    while (offset < byteCount) {
        AudioBuffer& buf = m_buffers[m_currentBuffer];
        WAVEHDR* hdr = static_cast<WAVEHDR*>(buf.header);

        // If buffer was previously submitted, wait for playback to finish
        if (hdr->dwFlags & WHDR_PREPARED) {
            while (!(hdr->dwFlags & WHDR_DONE)) {
                Sleep(1);
            }
            waveOutUnprepareHeader(hWave, hdr, sizeof(WAVEHDR));
        }

        size_t chunkSize = std::min(byteCount - offset, static_cast<size_t>(BUFFER_BYTES));
        std::memcpy(buf.data.data(), reinterpret_cast<const uint8_t*>(samples) + offset, chunkSize);
        if (chunkSize < BUFFER_BYTES) {
            std::memset(buf.data.data() + chunkSize, 0, BUFFER_BYTES - chunkSize);
        }

        hdr->dwBufferLength = static_cast<DWORD>(chunkSize);
        hdr->dwFlags = 0;

        if (waveOutPrepareHeader(hWave, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            waveOutWrite(hWave, hdr, sizeof(WAVEHDR));
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
