# Wade SDL Formats and Work Scaling

The preceding [guest-time measurements](38_guest_time_and_lcd_wait_diagnostics.md) show correct 200-Hz jiffies during Wade's measured fight, without LCD MMIO or ioctl activity in that interval. Function attribution now identifies the dominant SDL drawing routines. A separate entry/return probe establishes the application's requested display depth and the guest framebuffer driver's reply. These observations describe the emulated cartridge; they do not establish the original console's rendering path or speed.

## Function Attribution

The cartridge's SquashFS data stream starts at `0x170000` after removing 16 OOB bytes from each 528-byte NAND page. Extracted `usr/lib/libSDL-1.2.so.0.7.1` retains its local function symbols and debug information. Its SHA-256 is `8bbd10588364917697ac2070ee767f59df34803cac72a048e658d0486e9fdb93`. The loaded SDL base is `0x400f8000`, with translation table `0x30fa4000`.

The [function attribution](validation/2026-10-10_wade_sdl_functions.json) uses the preceding ratio-4 fight interval, modeled seconds 430–450. Symbol sizes define function bounds; the tool does not assign unrelated code to the nearest preceding symbol. Every function below matches the extracted function's complete code bytes in the final SDRAM capture.

| Function | Guest entry | Function bytes | Share of all PC samples |
| --- | --- | ---: | ---: |
| `BlitNtoN` | `0x401151e0` | 4,724 | 51.33% |
| `BlitNtoNKey` | `0x40117bf8` | 3,360 | 31.27% |
| `Blit2to2Key` | `0x40117a80` | 376 | 6.44% |
| `SDL_MixAudio` | `0x401034c4` | 636 | 0.33% |

The two dominant routines perform general pixel-format conversion; the second also checks a color key. This is function attribution, rather than a guess from neighboring exported symbols. It remains fixed-stride PC sampling, not exclusive function-cycle accounting. The byte comparison uses a final capture; it is not a general proof that mappings cannot change during a trace.

The [upstream SDL blitters](https://github.com/libsdl-org/SDL-1.2/blob/main/src/video/SDL_blit_N.c) provide reference behavior, but this cartridge's SDL includes additional 12-bit paths. Upstream source alone cannot identify the behavior of those local modifications.

## Requested and Returned Display Formats

The initial hypothesis was that SDL creates an RGB444 shadow surface because the application requests a depth that the framebuffer does not supply. Surface inspection alone did not establish that cause; it remained unverified until the function arguments, flags and ioctl payloads were examined.

The [SDL entry record](validation/2026-10-10_wade_sdl_mode_request.csv) captures:

```text
SDL_SetVideoMode(240, 160, 12, 0)
```

The flags argument is zero and does not contain `SDL_ANYFORMAT`. At step 353,702,813, `FBIOPUT_VSCREENINFO` (`0x4601`) receives a 240×160 request with `bits_per_pixel=12`. At step 353,704,655 it returns success and changes the structure to `bits_per_pixel=16`, with red offset/length 11/5, green 5/6 and blue 0/5. `FBIOGET_VSCREENINFO` (`0x4600`) also returns RGB565. The [decoded framebuffer records](validation/2026-10-10_wade_framebuffer_modes.csv) include successful requests and rejected mode probes; uninitialized GET request buffers are omitted.

The [captured SDL device](validation/2026-10-10_wade_sdl_surfaces.json) has a 240×160 RGB565 screen at `0x005d26c8`, and an RGB444 shadow/visible surface at `0x005d3028`. Both have a 480-byte pitch and two bytes per stored pixel. RGB444 surface storage here is **not** the packed three-bytes-for-two-pixels LCD format. The screen's flags are `0x80000021`; the shadow's flags are `0x80000020`.

Together, the request, driver reply and device fields support the SDL-created shadow-surface explanation **in this replay**. They do not show why the driver selects RGB565 or whether real hardware returns the same information. The emulator executes the guest kernel's ioctl handler; there is no host framebuffer-ioctl replacement. Subsequent [executable and framebuffer driver disassembly](41_wade_binary_and_s3c2410fb_analysis.md) resolves the `GPH8` (`0x56000074` bit 8) TV-Out detection in `s3c2410fb_probe`, the internal BGR444-to-RGB444 layer/sprite masks, and the 64-frame-per-unit counter cadence (`vblTime = 16` ms). No display format or cartridge setting is changed on the basis of this observation.

The [upstream SDL video implementation](https://github.com/libsdl-org/SDL-1.2/blob/main/src/video/SDL_video.c) describes format selection and shadow surfaces. Its current source is a reference rather than the cartridge's complete vendor source.

## Counter Scaling in the First Fight

Four replays reach the first fight against Rocco after the uninterrupted introduction. Input events preserve their modeled times by multiplying the ratio-4 script's instruction indices by `ratio / 4`; all scripted inputs are released before the fight. The comparison uses the first recorded counter values 89 and 84 while the fight remains active, rather than matching unrelated scenes at one fixed timestamp.

| CPU-work ratio | Step calls per modeled second | Counter 89 / 84 at modeled seconds | Interval | Step calls | Counter units per modeled second |
| --- | ---: | --- | ---: | ---: | ---: |
| 4 | 80,000,000 | 430 / 448 | 18 s | 1,440,000,000 | 0.278 |
| 6 | 120,000,000 | 297 / 309 | 12 s | 1,440,000,000 | 0.417 |
| 8 | 160,000,000 | 250 / 259 | 9 s | 1,440,000,000 | 0.556 |
| 16 | 320,000,000 | 247 / 252 | 5 s | 1,600,000,000 | 1.000 |

The [phase measurements](validation/2026-10-10_wade_counter_scaling.json) retain interval counters and script identities; [counter capture records](validation/2026-10-10_wade_counter_captures.csv) retain the intermediate visible values and raw-image hashes. Ratios 4, 6 and 8 each consume approximately 288 million step calls per displayed counter unit. Counter progress scales with the available guest work in this phase. This is evidence against a fixed-time counter rate in those replays; it does not establish the counter's original-console cadence or exclude a limiter in other scenes or at higher work allowances. One-second captures quantize the counter boundaries, and a capture can contain a partially written image.

Timer 4 expirations, requests and both acknowledgement totals advance by 200 per modeled second in every compared interval. Jiffies also advance by exactly 200 each second; no additional pending-source coalescence occurs. There are no LCD MMIO accesses or framebuffer ioctl calls in these fight intervals.

| Observed call or sampled quantity over counter 89 to 84 | Ratio 4 | Ratio 6 | Ratio 8 | Ratio 16 |
| --- | ---: | ---: | ---: | ---: |
| `gettimeofday` | 1,052 | 983 | 855 | 790,255 |
| `_newselect` | 319 | 322 | 326 | 313 |
| `poll` | 9 | 6 | 4 | 2 |
| Sampled framebuffer memory changes | 488 | 408 | 323 | 248 |

Time-query calls are not a high-frequency polling hotspot at ratios 4–8. At ratio 16, `gettimeofday` rises to 158,051 calls per modeled second and the captures show 89, 88, 87, 86, 85 and 84 in successive seconds. The [ratio-16 SDL attribution](validation/2026-10-10_wade_sdl_functions_ratio16.json) assigns 1.18% of all PC samples to `SDL_GetTicks`; the two general conversion blitters retain a combined 71.46%. Their complete sampled function bytes still match the extracted library. These observations suggest time-dependent polling once extra work is available. They do not yet identify its caller, prove a particular frame-limiter algorithm or validate ratio 16 as hardware speed. The 11% increase in step calls over the five counter units is also within the uncertainty of the one-second endpoint captures.

Wait-call counts alone do not identify whether each call blocks or polls. No `sched_yield`, `nanosleep`, `futex`, `clock_gettime` or `clock_nanosleep` calls are observed in these intervals. Function-entry counts and instructions per completed blitter call were not recorded by the sweep executable and cannot be recovered from its PC samples.

Framebuffer memory is sampled 100 times per modeled second. A slow partial drawing operation can therefore generate more sampled changes than the same operation at a higher ratio. The changing totals above must not be interpreted as rendered frames per counter unit. Even the derived steps-per-change values are affected by this sampling bias; they cannot establish conversion cost per frame.

## Clock and Hardware Calibration

The separate 30-second mode replay records [programmed clocks and CP15 selection](validation/2026-10-10_wade_clocks.csv). At its final boundary, CP15 control is `0xc0003177`, FCLK is 180 MHz, HCLK 90 MHz and PCLK 45 MHz. The emulator's execution-clock selection chooses FCLK for that control value. These values are derived from guest registers, not a physical-console clock measurement. The earlier sweep executable did not record CP15 control at every fight boundary.

If each of the roughly 288 million step calls per counter unit represents one executed ARM instruction, a scalar core issuing at most one instruction per cycle at 180 MHz needs at least 1.6 seconds per unit. Cache misses, multi-cycle instructions and memory waits would increase that duration. This is a conditional plausibility bound, not a calibrated speed: the probe counts CPU step calls, including exception entry, rather than retired instructions.

No verified continuous recording of Wade's digiBLAST fight counter is available for calibration. A hardware recording of the first Rocco fight covering, for example, counter 89 to 79 without cuts, pause or knockout would establish a useful cadence. The cartridge identity and counter endpoints should accompany the measured video duration. A GBA recording cannot establish this port's rendering path or speed. The missing hardware reference does not prevent inspecting the framebuffer driver, measuring complete blitter calls or improving interpreter throughput.

## Additional Probe Outputs

`scene_work_probe` adds `syscall_counts.csv`, counting every observed userspace syscall in each complete modeled-second interval. Counts include time and wait calls without generating an individual row for every `gettimeofday` invocation. Syscall numbers are checked against `include/asm-arm/unistd.h` in the [Linux 2.6.11 archive](https://cdn.kernel.org/pub/linux/kernel/v2.6/linux-2.6.11.tar.bz2): `gettimeofday=78`, `_newselect=142`, `sched_yield=158`, `nanosleep=162`, `poll=168`, `futex=240`, `clock_gettime=263` and `clock_nanosleep=265`.

An optional `WATCH_LIST` contains `TTB PC` pairs, in decimal or `0x` notation. `watched_entries.csv` records `r0`–`r3` and LR at matching userspace entries. In this mode, `framebuffer_ioctls.csv` records framebuffer structure words at syscall entry and return. Return matching requires the userspace PC, translation table and stack pointer to agree. Unsupported translations leave values blank.

Payload inspection structurally translates page tables and reads SDRAM directly. It does not change privileges, TLB state, fault registers or MMIO. This is diagnostic memory inspection, not an access-permission check. A file named `STOP` in the output directory ends a replay at the next complete modeled-second boundary. `completion.csv` records the actual budget consumed and whether that stop was requested.

`clocks.csv` records the CP15 control value, FCLK, HCLK, PCLK and selected execution clock at each complete modeled second. Clock derivation and selection do not change guest state.

## Validation and Limits

The repeated ratio-4 replay executes 36.8 billion CPU step calls through modeled second 460. All 460 one-second framebuffer captures match the preceding probe at the same boundaries. The additional syscall counts preserve the preceding fight profile, Timer 4 totals and jiffies progression.

The independent 30-second display-mode replay executes 2.4 billion steps. With entry/return inspection enabled, its final CPU register dump and complete SDRAM hash match the preceding diagnostic executable. SDRAM SHA-256 is `f676959050ca4ea2a31666bbbf272be77b106b954c3a097f500738b7a09be225`. The ROM-free suite reports 283 `PASS` lines and no failures; four additional Python tests cover symbol bounds, Thumb address markers, differing/unmapped captured code and translation-table separation.

The ratio-6, ratio-8 and ratio-16 replays stop at complete modeled-second boundaries 321, 318 and 259 respectively. The ratio-16 run consumes 82.88 billion step calls, reaches the first fight and provides the measured counter interval before stopping. These are headless diagnostic observations, not Windows audio or smooth-playback acceptance tests. Concurrent runs and host suspension make their host durations unsuitable for isolated throughput comparison.

CPU step calls include exception-entry work and are not exact retired-instruction counts. Sampled framebuffer changes are neither complete rendered frames nor LCD refreshes. Counts of time and wait syscalls do not by themselves distinguish a blocking wait from a zero-time poll. Hardware frame rate, effective ARM920T CPI, cache behavior and SDRAM wait states remain uncalibrated. A high ratio is a diagnostic work allowance, not a validated console-speed preset.

## Reproduction

```powershell
make test
python tests/guest_function_attribution.py
make build/scene_work_probe.exe
```

Use an independent erased 2,048-byte EEPROM image and the instruction-indexed `tests/wade_intro_ratio4.txt` input script. Scale each input step by `ratio / 4` for the other diagnostic ratios, preserving exact integer event boundaries. The common sweep executable has SHA-256 `8c37e4c1e03980dd182a519628aac3711a0dcdb860a32fcc94f86b12ea90b9d4`. The separate display-mode probe has SHA-256 `e55f6a89668928d8ab609a120db8190ed9469ac02de2a43fbb68f82504e455a7`.

```powershell
build/scene_work_probe.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  9200000000 4 build/wade-work initial.nvram tests/wade_intro_ratio4.txt
python tools/summarize_scene_timing.py build/wade-work --start 430 --end 450
```

For the mode trace, use a watch list containing `0x30fa4000 0x4011fe88`, a 600-million-tick budget, and pass the watch-list filename as the last argument. The later clock-recording build has SHA-256 `ada65e334964e1ce65b5027375e450fa5ef2bef7585ca7458f3801281bd999e3`. Extracted libraries, NAND data streams, EEPROM images, SDRAM captures and raw frames remain local.

```powershell
python tools/attribute_guest_functions.py libSDL-1.2.so.0.7.1 sdram.bin pcs.csv `
  --base 0x400f8000 --ttb 0x30fa4000 --start 430 --end 450 --output functions.json
```
