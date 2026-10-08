# Pitfall Original-Hardware Reference

## 1. Reference Recording

[Pitfall: The Lost Expedition on digiBLAST](https://www.youtube.com/watch?v=vzGiIXDxn_U) provides a reference for the console presentation and audio. The recording shows the Pitfall cartridge at approximately 00:15, the copyright screen at 01:00 and the title screen at 01:15. These are sampled positions where the images are visible, not measured transition times. Recording setup, microphone processing, volume and capture latency are unknown.

Comparison should align the same music phrase or game event before measuring tempo and pitch. Equal wall-clock positions in a video and an emulator capture are not sufficient: cartridge insertion, boot duration, menu input and omitted PCM gaps can differ. Recording coloration and room acoustics also prevent using raw amplitude or frequency response alone as a codec acceptance criterion.

The reference waveform has not yet been extracted and numerically aligned. No audio-equivalence or hardware-speed result is established by the video link alone.

## 2. Emulator Capture

Baseline: `5c23974`, including [consumed-sample DMA capture](20_pcm_capture_and_dma_sample_lifetime.md). Cartridge: `Pitfall The Lost Expedition [G] (EN).bin`. The capture starts from reset with default device initialization and no input script or persistent EEPROM file.

Cartridge SHA-256: `f089a98d7daa6edb86f2b92f244a33f463942fe683f26592a1c96618093b9ac4` (17,301,504 bytes).

```powershell
make build/capture_audio.exe
New-Item -ItemType Directory -Force build/diagnostics
build/capture_audio.exe "roms/games/Pitfall The Lost Expedition [G] (EN).bin" 1600000000 build/diagnostics/pitfall.wav
```

The capture completes 1.6 billion steps and produces 1,626,477 stereo frames at 22,050 Hz, or 73.763129 seconds of emitted PCM. The first nonzero sample occurs at output frame 492,811, or 22.349705 PCM seconds, in the callback recorded at step 567,158,891. This is an audio-content landmark, not elapsed time from console power-on. The WAV omits DMA production gaps and host scheduling, as described in the capture tool documentation.

[Numeric observations](validation/2026-10-08_pitfall_generated_pcm.csv) cover the final twenty seconds:

| Property | Observation |
| --- | ---: |
| Peak absolute signed 16-bit sample | 13,568 |
| RMS across both channels | 1,735.36 |
| Samples with absolute value at least 32,760 | 0 |
| Callback boundaries measured | 3,667 |
| Mean adjacent left-channel change at boundaries | 334.47 |
| Boundary changes greater than 10,000 | 0 |
| Samples with low eight bits zero | 100% |
| Identical left/right channels | No |

The absence of clipping or large callback-boundary changes does not establish good audible quality. The low-byte pattern suggests limited precision in the generated PCM, but does not distinguish game source material from mixer behavior or prove an incorrect IIS word length. Observed callbacks use inferred rates of 87,890 Hz during initialization and 22,050 Hz afterward; both are converted to the WAV output rate.

## 3. Matching Display Landmarks

A separate headless run of the same baseline, without sound or input, completes 800 million steps. A snapshot at 400 million steps shows the copyright screen in packed RGB444 with a 360-byte scanline stride. The final framebuffer at 800 million steps shows the title screen in the same format. Both correspond to motifs visible in the reference recording. They do not establish equivalent transition timing, sustained gameplay or synchronized audio.

Reproduction:

```powershell
bin/oceanblast.exe "roms/games/Pitfall The Lost Expedition [G] (EN).bin" --steps 800000000 --snapshot-interval 400000000
```

Run in a local diagnostics directory because the command writes memory and framebuffer artifacts. These assets must remain local.

## 4. Pending Comparison

Manual comparison of the first twenty seconds of nonzero generated PCM against the reference title music reports **slower playback, stalls and crackling**. This rejects audible acceptance despite the absence of large callback-boundary discontinuities in the measured tail. The comparison has not quantified the speed ratio or aligned the full reference waveform.

Align the beginning of the same title-music phrase in a reference audio excerpt and the generated PCM. Compare phrase duration, stable pitches and discontinuities separately; then repeat with live host playback to isolate queue starvation. A digital reference excerpt or a recording with known settings is required for a reproducible waveform comparison. No clock, pitch or codec correction has been made solely to match an unmeasured recording.

## 5. CPU/Peripheral Ratio Probe

A diagnostic capture executes two CPU steps per peripheral tick while retaining the same IIS rate interpretation and WAV output rate. Its 2.4-billion-step limit corresponds to approximately 1.2 billion peripheral ticks, whereas the baseline executes 1.6 billion of each. These are different total emulated durations. Compare the first twenty seconds after nonzero PCM begins, rather than their final intervals.

```powershell
build/capture_audio.exe "roms/games/Pitfall The Lost Expedition [G] (EN).bin" 2400000000 build/diagnostics/pitfall_cpu2.wav 2
```

| First twenty seconds after PCM onset | One CPU step per tick | Two CPU steps per tick |
| --- | ---: | ---: |
| Complete 512-byte callback portions in the interval | 3,407 | 3,445 |
| Portions identical to the preceding complete portion | 579 | 416 |
| Distinct portions | 1,373 | 1,957 |

[Probe measurements](validation/2026-10-08_pitfall_cpu_ratio_probe.csv). Portion hashes identify exact PCM repetition; repeated musical waveforms can also produce equal portions. The excerpts begin at nonzero PCM onset, without independent musical-phrase alignment. Reduced repetition is a reason to investigate guest production deadlines, not proof that the changed ratio is accurate or that distortion is resolved. The production launcher retains its existing clock model.
