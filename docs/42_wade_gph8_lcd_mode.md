# Wade with the TV-Out Strap High (`GPH8 = 1`)

The [executable and driver analysis](41_wade_binary_and_s3c2410fb_analysis.md) found that `s3c2410fb_probe` reads `GPHDAT` bit 8 (`GPH8`) and selects the 16-bpp `PAL` modes when it is low. This document records the first test with that pin read as high. It is a diagnostic option; the default is unchanged.

## Emulator Change

`Bus::readMmio` now models `GPHDAT` (`0x56000074`) as an input-pin register: when the environment variable `OCEANBLAST_GPH8=1` is set and `GPHCON` bits `[17:16]` configure the pin as an input, bit 8 reads as 1. Without the variable, behavior is identical to before.

A first attempt that seeded the register at reset had no effect (step counts identical to the baseline). The guest writes `GPHDAT` five times during boot (value `0x10`), which replaced the seeded value. Real input pins do not take their level from the output latch, so the level is applied at read time.

## Driver Reply

With `OCEANBLAST_GPH8=1` the kernel's `FBIOPUT_VSCREENINFO` reply to `SDL_SetVideoMode(240, 160, 12, 0)` returns `bits_per_pixel = 12` (baseline: 16). The captured frames are 57,600 bytes (packed 12-bpp, 360-byte rows) instead of 76,800 bytes. This confirms the strap selects the `'LCD'` mode as the disassembly predicted.

## Frame Rate at Ratio 4

The [per-second records](validation/2026-10-10_wade_gph8_frame_rate.csv) cover a 460-second replay with the ratio-4 input script (36.8 billion step calls). `_newselect` is called once per frame by `VBlankIntrWait`, so it is a frame counter.

| Interval (modeled seconds) | `_newselect` per second | `gettimeofday` per second |
| --- | ---: | ---: |
| 40–260 (intro, menus) | 62–63 | 115,000–178,000 |
| 280–340 (heavier scene) | 27–45 | about 90 |
| 360–460 (including the interval used for the earlier fight analysis) | 62–63 | 115,000–117,000 |

In the baseline (16-bpp) run the same fight interval needed 4.5 million steps per frame and ran at about 18 frames/s (0.278 counter units/s, 64 frames/unit). With the strap high, the frame limiter in `vblank_wait()` is active: frames finish within the 16 ms budget (1.28 million step calls at ratio 4), so the loop polls `SDL_GetTicks()` and the game runs at the programmed 62.5 frames/s, i.e. 1.024 s per counter unit.

## Limits

- The counter was derived from the frame rate (64 frames per unit, see document 41), not read from captured images in this run.
- The heavier scene at 280–340 s still runs below 62.5 frames/s at ratio 4.
- That GPH8 is high on the real console without a TV-out cable follows from the driver enabling its pull-up and from the `'LCD'` mode being the handheld mode. It has not been confirmed on hardware.
- Other cartridges have not been tested with the strap high. Boot splashes and video cartridges use the same driver. The default is therefore unchanged.
- The probe executable used has SHA-256 `71baaf879dd4c1ec234461a79e76ef2e6858db8b50e5930f18aeb8b794415cd3`.

## Reproduction

```powershell
$env:OCEANBLAST_GPH8 = '1'
build/scene_work_probe.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  9200000000 4 build/gph8-fight initial.nvram tests/wade_intro_ratio4.txt
```
