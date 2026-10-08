# Audio Sample Rate Resolution & Dynamic Resampling

Date: 2026-10-07. Baseline: `06142fd`.
Follow-up: [2026-10-08 audio timing investigation](11_audio_timing_followup.md) corrects the remaining rate-conversion, buffering and DMA-pointer issues. This document describes the earlier implementation, not acceptance of all cartridges.
Reported symptom: Audio sounds like it is playing at double speed (pitch shifted up one octave) and stutters heavily during gameplay.

---

## 1. Root Cause Analysis

### A. S3C2410 IIS Hardware Clock & Prescaler Decoding
During boot and audio playback, the guest Linux ALSA driver (`s3c2410-iis` + `cs43l43`) configures the S3C2410 IIS audio controller via memory-mapped registers:
- `IISMOD` (`0x55000004`) is set to `0x99`:
  - Bits [7:6] = `0b10` -> Transmit mode (`S3C2410_IISMOD_TXMODE`)
  - Bit 4 = `1` -> MSB-justified serial format (`S3C2410_IISMOD_MSB`)
  - Bit 3 = `1` -> 16-bit word length (`S3C2410_IISMOD_16BIT`)
  - Bit 1 = `0` -> 256fs master clock ratio (`S3C2410_IISMOD_256FS`)
  - Bit 0 = `1` -> 32fs bit clock (`S3C2410_IISMOD_32FS`, 16-bit Left + 16-bit Right)
- `IISPSR` (`0x55000008`) is set to `0xE7`:
  - Prescaler A = `(0xE7 >> 5) & 0x1F` = `7`
  - Prescaler B = `0xE7 & 0x1F` = `7`
- PCLK is 45,000,000 Hz (45 MHz, configured via `MPLLCON = 0x52011`, `CLKDIVN = 0x3`).

Using the Samsung S3C2410 IIS hardware formula:
$$\text{CODECLK} = \frac{\text{PCLK}}{\text{Prescaler A} + 1} = \frac{45\,000\,000}{7 + 1} = 5\,625\,000 \text{ Hz}$$
$$f_s = \frac{\text{CODECLK}}{256} = \frac{5\,625\,000}{256} = 21\,972.65625 \text{ Hz} \approx \mathbf{22\,050 \text{ Hz}}$$

The guest operating system generates an audio stream sampled at **22,050 Hz stereo** (16-bit PCM, 88.2 kB/s).

### B. Host Sample Rate Mismatch & Buffer Starvation
- In `src/main.cpp`, the host audio backend was hardcoded to:
  `audio.init(44100, 2);`
- Feeding 22,050 Hz samples into a 44,100 Hz output produced:
  1. **Double Playback Speed:** The audio tempo was multiplied by $44100 / 22050 = 2.0\times$.
  2. **Pitch Shift:** Frequencies were transposed up by exactly 1 octave ($\times 2.0$), producing high-pitched audio.
  3. **Recurrent Buffer Starvation & Stuttering:** The host sound card consumed audio buffers twice as fast (176.4 kB/s) as the guest emulator produced them (88.2 kB/s). The host buffer queue drained completely dry every period, causing repetitive audio dropouts and stuttering.

---

## 2. Technical Implementation

### 1. Dynamic Hardware Sample Rate Detection (`src/memory/bus.cpp`, `src/memory/bus.h`)
Added `Bus::getAudioSampleRate()` which calculates the active sampling rate directly from the guest S3C2410 `IISPSR` and `IISMOD` registers and maps them to standard rates:
- Prescaler division factor $\approx 22\,050 \text{ Hz}$ -> returns `22050`
- Prescaler division factor $\approx 44\,100 \text{ Hz}$ -> returns `44100`
- Prescaler division factor $\approx 11\,025 \text{ Hz}$ -> returns `11025`
- Prescaler division factor $\approx 8\,000 \text{ Hz}$ -> returns `8000`

### 2. Dynamic Audio DMA Pacing (`src/memory/bus.cpp`)
Updated DMA Channel 2 timer calculation to adapt dynamically to the detected sample rate:
```cpp
u32 rate = getAudioSampleRate();
u32 bytesPerSec = rate * 4; // 16-bit stereo PCM
u32 cyclesPerByte = (bytesPerSec > 0) ? (20000000 / bytesPerSec) : 227;
dma2Timer = (dma2Count > 0) ? (dma2Count * cyclesPerByte) : 150000;
```
For 22.05 kHz stereo, this provides 226 cycles/byte (~46.4 ms per 4KB buffer at ~20 MIPS).

### 3. Native 22,050 Hz waveOut Support with 44.1 kHz Fallback (`src/audio/audio_win32.cpp`, `src/audio/audio.h`)
- Defaulted `Audio::init()` to 22,050 Hz stereo 16-bit PCM.
- If a host sound device rejects 22,050 Hz, `init()` automatically falls back to standard 44,100 Hz.

### 4. Initial Linear-Interpolation Resampler (`src/audio/audio_win32.cpp`)
- `Audio::writeSamples(samples, count, inputSampleRate)` compares the incoming stream rate against the opened `waveOut` device rate.
- When input is 22,050 Hz and device is 44,100 Hz, an internal resampler generates 2x interpolated frames:
  $$L_{\text{out}}[2i] = L[i], \quad R_{\text{out}}[2i] = R[i]$$
  $$L_{\text{out}}[2i+1] = \frac{L[i] + L[i+1]}{2}, \quad R_{\text{out}}[2i+1] = \frac{R[i] + R[i+1]}{2}$$
- When input is 44,100 Hz and device is 22,050 Hz, 2x decimation is applied.
- These two conversions address the original rate mismatch. They do not establish correct timing at other input rates, uninterrupted playback or hardware-equivalent pitch.

### 5. CLI & Launcher Options (`src/main.cpp`)
- Added `--audio-rate <Hz>` (alias `--rate`, `--samplerate`) flag to allow overriding the host sample rate.
- Main loop forwards `bus.getAudioSampleRate()` dynamically to `audio.writeSamples()`.

---

## 3. Verification & Results

- **Unit & Regression Tests:** `make test` executed cleanly:
  - 15 / 15 CPU & DMA regression tests: **PASS**
  - 7 / 7 Keypad & GPIO tests: **PASS**
  - Total: **22 / 22 PASS (0 failures)**
- **Initialization Log:**
  `[Audio] Win32 waveOut initialized (22050 Hz, 2 channels, 16-bit PCM, 16 buffers).`
- **Audio Timing & Pitch:** The original rate mismatch was addressed. Per-cartridge pitch, underruns and audio/video synchronization remain subject to runtime verification; the later investigation found additional failures.
