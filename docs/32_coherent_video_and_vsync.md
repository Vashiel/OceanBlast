# Complete Video Frames and Synchronized Presentation

## Framebuffer Writes Behind Mixed Images

The player writes a framebuffer directly from its software color-conversion loop. A wall-clock display sample can therefore observe an old region beside a newly written region. Copying that sample atomically on the host preserves the mixed image; host vertical synchronization alone cannot repair it.

[Instruction-level store observations](validation/2026-10-09_video_write_bursts.csv) identify complete origin-first sweeps in both tested layouts:

| Cartridge | Scanout | Complete uninterrupted write bursts inspected | Median instructions from first to last store |
| --- | --- | ---: | ---: |
| Winx Club (IT/ES) | RGB565, 240 rows, 480-byte stride | 192 | 2,121,806.5 |
| Sonic X (IT/ES) | Packed RGB444, 160 rows, 360-byte stride | 273 | 1,225,221 |

These bursts come from matched five-billion-instruction replays at the diagnostic 4x instruction allowance. At the nominal 80-MIPS allowance, the median Winx sweep alone spans about 26.5 ms, exceeding the old 16-ms host sampling period. Lower achieved throughput extends both sweeps. Interrupts and scheduling can divide a sweep into multiple bursts, so a quiet interval is not a reliable completion boundary. Burst counts are not total decoded or displayed frame counts.

## Host Frame Capture

`FrameLatch` observes successful SDRAM stores and records byte coverage from the framebuffer origin. It copies the entire framebuffer into a separate host image only after all scanout bytes have been written. A new origin store abandons incomplete coverage rather than combining it with a later sweep. The committed image remains unchanged while the next image is built, including across guest interrupts and thread scheduling.

Automatic capture covers the observed 240-row video surfaces and native 160-row packed surfaces after a mapped ELF export identifies `RV40toYUV420Transform`. This reads ELF metadata without executing or patching guest code. Only tightly packed RGB444/RGB565 rows are selected. Ordinary 160-row game surfaces retain their existing presentation path. Changed LCD address or geometry discards obsolete capture state. Unsupported partial-update layouts require an independently established completion boundary; the byte-coverage rule must not be generalized to them without validation.

Frame capture observes writes without changing guest RAM, interrupts, clocks, decoder results or input positions. It is a host presentation correction, not a cycle-accurate LCD scanout model or an emulated video accelerator. The traced ARM color-conversion code confirms CPU-side conversion for these frames; it does not establish every hardware capability of OCEAN-L-20.

## Windows Presentation

The default Windows renderer uses a Direct3D 11 texture and a two-buffer DXGI flip-discard swap chain. `Present(1, 0)` synchronizes presentation with vertical blank and does not enable tearing. An independent presentation thread receives copies of complete host images; its vertical-blank wait does not block the CPU interpreter. The pending image queue holds only the latest submitted image, preventing unbounded latency when production exceeds presentation speed.

The renderer retains full programmed source height and scales to the existing window area. Initialization or device failure reports a GDI fallback. GDI still receives committed video frames, but the fallback does not provide the same explicit synchronization guarantee. The window title reports `VSync` or `GDI`. Shutdown keeps sent Windows messages flowing until the presentation thread exits, avoiding a message-pump dependency while joining it.

Microsoft documents [vertical-blank synchronization and message-pump interaction](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present) and [flip-model presentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model). Host GPU presentation does not move guest RealVideo decoding to the GPU.

## Validation and Diagnostics

The regression suite passes 235 checks. Five Windows checks validate the full source height, the displayed DXGI red/green reference image, restoration of 160-row game output in both GDI reference decoding and DXGI texture output, and successful synchronized presentation. The screen-pixel check declares DPI awareness before creating its test window so desktop coordinate scaling does not invalidate the sample.

The capture tests cover incomplete images, unchanged committed bytes during later writes, abandoned sweeps, duplicate stores, coverage-word boundaries, address changes and byte/halfword/word bus paths. [Four five-billion-instruction GUI replays](validation/2026-10-09_complete_video_frames.csv) at the diagnostic 4x allowance complete normally: Sonic X captures 353 sweeps, Winx (IT/ES) 397, Winx (NL/FR/EN/TR) 373 and Totally Spies (IT) 427. All four preserve the previous complete SDRAM image and displayed CPU-register dump. No incomplete origin-restarted sweep is published. Periodic samples find 1, 4, 4, 3 live-versus-committed differences respectively; the NL Winx replay also ends during an in-progress write, while retaining its last complete presentation image.

[Eleven game smoke replays](validation/2026-10-09_presentation_game_smoke.csv) reach two billion instructions without guest segmentation faults or kernel panic. They are headless observations without button inputs and do not establish gameplay. Separate [matched-input replays](validation/2026-10-09_presentation_game_states.csv) for Crazy Jack, Superstar Chefs and Wade retain the previous SDRAM and displayed CPU registers with `tests/start_buttons.txt`. The earlier comparison used this script, so runs without it cannot establish state equivalence.

A separate [reverse-order headless throughput comparison](validation/2026-10-09_frame_capture_throughput.csv) runs Superstar Chefs twice per executable with 2.4 billion instructions, the 4x allowance, blank EEPROM and `tests/chefs_ratio4_confirm.txt`. All four final SDRAM images match. Mean wall-time throughput is 53.809 MIPS for the preceding executable and 52.330 MIPS for the candidate, a -2.75% difference. These two samples per build include loading and shutdown, use different compiler training workloads, and do not isolate the store observer cost or establish a general speed change.

Cartridge replays run concurrently during parts of this validation. Their audio starvation counters and wall times are not isolated timing or listening acceptance.

`performance.csv` adds cumulative `coherent_frames`, `partial_sweeps`, `synchronized_presentations`, `frame_capture_active` and `capture_ready` fields. The presentation counter belongs to the active DXGI backend and counts successful Present calls, rather than independently measured physical display scans. Complete sweeps can include blank startup frames; the counter is not a decoder-frame count. Existing update-rate fields count host submissions, which can differ from synchronized presentation count. Diagnostic snapshots retain the live framebuffer and add `_present.raw` for the last committed source image. Final `fb_present.raw` contains that committed image.

```powershell
make test
make build/display_height_win32.exe
build/display_height_win32.exe
bin/oceanblast.exe "video-cartridge.bin" --gui --sound --profile `
  --frame-sync auto --renderer auto --cpu-steps-per-tick 4
```

For comparison, `--frame-sync raw --renderer gdi` selects unlatched framebuffer sampling and the previous GDI output path. Keep the same EEPROM image, input script and instruction allowance when comparing guest state. The new presentation path does not establish correct original-hardware speed, continuous sound, audio/video synchronization or visual acceptance on every cartridge.
