# RealVideo Decode and Frame Delivery

## Decoder Output Behind Black Screens

Sonic X and the Italian/Spanish Winx video cartridge decode chromatic 240x160 YUV420 images while their visible content remains dark at the standard instruction allowance. The decoder output therefore supplies a concrete boundary for investigating the black screen. It does not support treating these video cartridges as the damaged international Winx game/video combination.

Local ELF inspection identifies the loaded exports `RV40toYUV420Transform` and `RADecode`. Library placement changes with execution conditions, so a code-page address from one replay cannot identify a decoder in another replay without checking its mapping. `tools/inspect_guest_elf.py` reads ELF metadata from a local SDRAM dump. `tools/codec_probe.cpp` can discover the video transform export in the running guest and observe its calls and returns.

The probe records guest translation table, input/return instruction positions, result code, output dimensions, YUV-buffer readability and luma/chroma statistics. It samples the returned buffer rather than substituting a decoder or modifying guest code. The five-argument transform interface and output width/height interpretation are documented by the [MPlayer RealVideo wrapper](https://sources.debian.org/src/mplayer/2%3A1.5%2Bsvn38681-10/libmpcodecs/vd_realvid.c). The guest's observed output dimensions agree with that interface.

## Matched Instruction Budgets

Each cartridge executes five billion instructions at ratios 1 and 4, with identical blank initial EEPROM images and `tests/video_language_confirm.txt`. These are headless functional probes without Windows sound output or pacing. Input positions are identical in instructions; their peripheral-time positions differ between ratios. The comparisons do not match movie timestamps or claim original-hardware speed.

[Decoder and display observations](validation/2026-10-09_video_decoder_delivery.csv) record:

| Cartridge | Allowance | Completed transform returns | Chromatic readable YUV buffers | Final visible content |
| --- | ---: | ---: | ---: | --- |
| Sonic X (IT/ES) | 1x | 500 | 472 | Uniform dark gray |
| Sonic X (IT/ES) | 4x | 409 | 382 | Colored episode imagery |
| Winx Club (IT/ES) | 1x | 331 | 286 | Black |
| Winx Club (IT/ES) | 4x | 441 | 396 | Colored episode imagery |

All recorded transform returns report success. Readability and chroma classification apply to the inspected returned buffers, not every internal decoder frame. The chroma classification uses observed U/V extrema outside 120..136. Dark startup frames are valid decoded output; nonzero byte counts alone cannot distinguish them from visible content.

The original content classification inspects the upper 240x136 pixels of the final active framebuffer, excluding the lower player controls. Sonic uses packed RGB444 with 360-byte rows in both comparisons. Winx ends with RGB565 and 480-byte rows in both. No display override is applied. The 4x buffers contain episode imagery and hundreds of distinct content colors; the 1x content is uniform despite chromatic decoder output.

The increased instruction allowance permits decoded imagery to reach the visible buffer in these replays. This narrows the remaining cause to execution time, player clock/scheduling and frame delivery. It does not yet identify a specific late-frame discard branch or prove that the instruction ratio is correct for the console. Raising the instruction allowance increases the host throughput required by audio deadlines, as quantified in [Gameplay Work and CPU Execution Cost](29_gameplay_work_and_execution_cost.md).

These original framebuffer captures covered only 160 rows. Subsequent LCD geometry inspection found that the video player programs 240 rows, so color counts and matching captures establish image delivery but not correct framing. [Programmed scanout height](31_programmed_scanout_height.md) describes the cropping correction.

The original captures covered only 160 rows. Subsequent LCD inspection found that the player programs 240 rows: color counts establish image delivery, but not correct framing. [Programmed scanout height](31_programmed_scanout_height.md) describes the cropping correction.

Additional five-billion-instruction probes at 4x also produce colored episode imagery for Winx Club (NL/FR/EN/TR) and Totally Spies (IT), using the same blank device image and input script. The [additional observations](validation/2026-10-09_additional_video_delivery.csv) record 407 and 472 successful transform returns respectively. These two probes have no matched 1x or GUI/audio comparison.

## GUI Confirmation and Limits

The installed optimized executable repeats the Italian/Spanish Winx video replay with GUI and sound at 4x. It reaches five billion instructions, closes normally and produces the same final framebuffer as the headless transform probe. [GUI metadata](validation/2026-10-09_winx_video_gui.csv) records that match and the executable identity.

A decoder probe runs concurrently during part of the GUI confirmation, so this is functional output validation rather than an isolated performance or audio comparison. Its last performance record contains 104 empty-queue observations and zero dropped samples. Continuous, correctly timed sound and audio/video synchronization remain unverified.

Other video cartridges require independent replay and listening acceptance. The corrupted international combined Winx image remains a separate source-data issue. These observations do not repair its damaged filesystem blocks or establish full compatibility across every cartridge.

## Reproduction

```powershell
make build/codec_probe.exe
build/codec_probe.exe "extracted-video-roms/Winx Club 1 [V] (IT ES).bin" `
  5000000000 4 build/winx-decoder blank.nvram tests/video_language_confirm.txt auto
bin/oceanblast.exe "extracted-video-roms/Winx Club 1 [V] (IT ES).bin" `
  --gui --sound --profile --cpu-steps-per-tick 4 --steps 5000000000 `
  --exit-on-limit --nvram device-copy.nvram --input-script tests/video_language_confirm.txt
python tools/inspect_guest_elf.py build/winx-decoder/sdram.bin 0x30db8000 build/winx-elf.json
```

`blank.nvram` is a 2,048-byte image filled with `0xff`; initialize the GUI's independent `device-copy.nvram` from the same image. Use the translation table actually reported by the replay when inspecting ELF metadata. A missing export or zero observed calls requires checking mapping and startup progress before interpreting it as a decoder failure.

The probe stores initial and latest decoded YUV buffers, final SDRAM and framebuffer captures locally. Those assets are ignored by Git and are not distributed. Published records contain observations and hashes only. The next diagnostics should correlate returned decoder timestamps with the player's presentation clock, renderer entry/return events and buffer submission, while preserving functional comparisons across CPU execution changes.
