# Superstar Chefs Performance Collapse Analysis and Fix

## Reported Symptoms

User testing of `Superstar Chefs [G] (EN).bin` (`17,301,504` bytes, CRC32 `0x93209877`) identified three severe runtime issues under the previous automatic `2x` (`40M` instructions per modeled second) cartridge preset:

1. **Prolonged frozen loading screen:** After selecting a slot or starting the game, the loading screen remains static for a long period before transitioning to the title or level screen.
2. **Slow title screen music and animation:** Title music and fruit animations start sluggishly and exhibit audible stutter when host throughput falls below target pacing.
3. **Sudden mid-game frame-rate collapse:** Level 1 begins at a playable speed for the first several seconds and then abruptly collapses into extreme slow motion (~2–3 FPS).

---

## Guest Binary Extraction and Engine Architecture

The cartridge's SquashFS 2.0 filesystem at NAND data offset `0x0018C000` (`4,480,387` bytes) contains an unstripped ARM ELF32 executable `SuperstarChefs.elf` (`1,278,883` bytes, SHA-256 `672753bc109f48e04e393a0944626ad8a6fd5ec29f1a1869b1031e39f9566e26`) with `3,467` symbol-table entries and DWARF debug metadata.

Disassembly of `_Z8GAME_Runv` (`0x02fd70`) establishes the per-frame main loop:

1. `_Z12UpdateTimersv` (`0x01d4b8`) — reads `SDL_GetTicks()` (`GetHiResTime` at `0x00b6e4`), computes `frameTicks = (now - lastTimer) >> 2` (250 Hz tick units), and clamps `frameTicks` at `32` (`128 ms`).
2. `_Z11GAME_Updatev` (`0x02ee70`) — dispatches state-specific logic (`UpdateTitle`, `UpdateLoading`, `UpdateMenu`, `UpdatePickLevel`, `UpdateMap`).
3. `_Z9GAME_Drawv` (`0x02f06c`) — dispatches state-specific software rendering (`DrawTitle`, `DrawLoading`, `DrawMenu`, `DrawPickLevel`, `DrawMap`).
4. `_Z8UpdateFSv` (`0x00b838`) — transfers or downsamples the internal rendering surface (`draw_base`) into the SDL framebuffer surface and calls `SDL_UpdateRect` (`FB_LCD444Update` at `0x4004b754`).
5. `_Z10CheckSoundv` (`0x00c0b8`) — updates `libmikmod` (`MikMod_Update`) while a separate `libpthread` SDL audio thread continuously mixes 64-sample stereo blocks at `11,025 Hz` (`172.3` `write(2)` calls per modeled second to `/dev/dsp`).

---

## Root Cause 1: Frozen Loading Screen (`_Z9InitAudiov` and Kernel `NWFPE` Emulation)

Disassembly of `_Z13UpdateLoadingv` (`0x02a2a8`) shows that on loading frame 5 (`0x02a2ec`), the main thread calls `_Z9InitAudiov` (`0x022278`). Rather than loading pre-rendered PCM files from SquashFS, `_Z9InitAudiov` **procedurally synthesizes 14 sound effects in software** at startup (`CreateSelectFX`, `CreateConfirmFX`, `CreateJumpFX`, `CreateFloorFX`, `CreateStompBadFX`, `CreateFruitTakenFX`, `CreateFruitX2FX`, `CreateGemBounceFX`, `CreateSuperPillFX`, `CreateSuperPillEndFX`, `CreateExtraLifeFX`, `CreateElevatorOpenFX`, `CreateElevatorCloseFX`, `CreateElevatorEnterFX`, `CreateButtonFX`, `CreateButtonDropFX`, `CreateEnterNameFX`, and `CreateRecipeFoundFX`).

Inspection of the waveform synthesis primitives reveals why this startup synthesis is extraordinarily expensive on the S3C2410 ARM920T:

| Function | Address | Size (bytes) | FPA11 Floating-Point Instructions |
| --- | --- | ---: | ---: |
| `_Z11AddSineTonePsi5fixedS0_S0_S0_S0_` | `0x01f74c` | `512` | `20` (`ldc`/`mcr`/`cdp`/`mrc` `p1`/`p2`) |
| `_Z10AddSawTonePsi5fixedS0_S0_S0_S0_` | `0x01f94c` | `380` | `9` |
| `_Z15AddTriangleTonePsi5fixedS0_S0_S0_S0_` | `0x01fac8` | `424` | `12` |
| `_Z13AddSquareTonePsi5fixedS0_S0_S0_S0_` | `0x01fc70` | `364` | `9` |
| `_Z7TremoloPsi5fixedS0_` | `0x01ff58` | `444` | `27` |

Across the entire `SuperstarChefs.elf` binary, `101` functions contain ARM FPA11 (`p1`/`p2`) hardware floating-point coprocessor instructions. Because the Samsung S3C2410 (ARM920T) has no hardware FPU, every FPA11 instruction sequence traps through the Undefined Instruction vector (`0xFFFF0004` -> `__und_usr` at `0xc00192e0`) into the guest Linux kernel's **NetWinder Floating Point Emulator (`NWFPE`, `0xc0024a00..0xc002b580`)**.

In a 60-modeled-second full-system probe at `cpuStepsPerTick = 2` (`40M` instructions/s), the loading screen remains completely frozen (`FB_Chg = 0`) for **14 modeled seconds (`Seconds 9..22`)**:

| Execution Region (`Seconds 9..22`, Ratio 2) | Sampled Instructions | Share of Total CPU |
| --- | ---: | ---: |
| Kernel `NWFPE` + `__und_usr` (`0xc0024000..0xc002b000`, `0xc0019000`) | `362,598` | `65.33%` |
| SquashFS `zlib_inflate` + NAND `__raw_readsb` (`0xc00b5000..0xc00bc000`) | `81,580` | `14.70%` |
| User-space `SuperstarChefs.elf` synthesis loops (`0x01f000..0x022000`) | `20,577` | `3.71%` |
| Remaining kernel/driver activity | `90,250` | `16.26%` |

---

## Root Cause 2: Title Screen Float Traps and Audio Mixing

Once the title screen appears (`Seconds 23..28` at Ratio 2), `_Z15DrawTitleFruitsiii` (`0x028cc8`, containing `111` FPA11 instructions) and `_Z9DrawTitlev` (`0x02a0e4`, containing `28` FPA11 instructions) execute floating-point scaling and trajectory math on every frame while `libmikmod`'s `AddChannel` (`0x05d74c`) mixes the title module `SSChefs Title (OZ)`:

* Kernel `NWFPE` floating-point trap emulation consumes **`29%–39%`** of all instructions on the title screen.
* `AddChannel` (`0x05d74c`) consumes **`21.25%`** (`50,552 / 237,859` samples).
* `_Z7CKBlittiiP7SURFACEiiii` (`0x00ce04`) consumes **`10.97%`**.

---

## Root Cause 3: Why Gameplay Starts Normal and Then Suddenly Collapses

Disassembly and per-second profiling of Level 1 (`Seconds 31..60` at Ratio 2) reveal three interacting mechanisms that cause the sudden mid-level collapse:

### 1. Internal `480x320` Resolution Switch (`SetDouble(1)`) and Software 2x2 Box-Filter Downsampling (`_Z8UpdateFSv`)

In `_Z9GAME_Drawv` (`0x02f06c`), states `0` (Title) and `1` (Loading) call `_Z7ZoomSeti(0)` -> `_Z9SetDoublei(0)` (`0x00bac0`), which renders directly at the native `240x160` LCD resolution (`draw_width = 240`, `draw_height = 160`).

When entering `PickLevel` (state `3`) or `In-Game` (state `7` at `0x02f178`), the game calls `_Z7ZoomSeti(1)` -> `_Z9SetDoublei(1)` (`0x00bac0..0x00bc18`), which allocates a **`480x320` internal 16-bpp rendering buffer (`draw_base`, `307,200` bytes — 4x the pixel count of the physical LCD)**. Every gameplay frame:

1. Renders all tile layers and actor sprites into the `480x320` buffer (`_Z7CKBlittiiP7SURFACEiiii` at `0x00ce04`).
2. Calls `_Z8UpdateFSv` (`0x00b838`), which downsamples the `480x320` buffer down to `240x160` in software by averaging each `2x2` pixel block in 12-bpp (`0x00b9e4..0x00ba70`, `36` ARM instructions including 4 `ldrh` and 1 `strh` per output pixel = **`1,382,400` instructions per frame**) before calling `SDL_UpdateRect` (`FB_LCD444Update` at `0x4004b754`).

### 2. Multi-Channel `.XM` Music Surge in `libmikmod` `AddChannel` (`0x05d74c`)

`SuperstarChefs.elf` statically links `libmikmod`'s high-precision mixer (`virtch2.c`) compiled with `FRACBITS = 28` (`1 << 28` fixed-point fraction). In `AddChannel` (`0x05d74c`):

* Whenever a sample offset `index >= 8` (`index >> 3 != 0` at `0x05d7ec`), `AddChannel` cannot use 32-bit shifts without overflowing `(index << 28)`.
* It therefore branches to the **64-bit `SINT64` (`long long`) interpolation loop (`0x05dbf4..0x05dd58`)**, which executes **`53` ARM instructions per stereo sample per active voice** (`~584,325` instructions/s per active voice at `11,025 Hz`).

During the **first 7 seconds of Level 1 (`Seconds 31..37` at Ratio 2)**, the level music module `SSChefs Game (OZ)` is in its sparse intro:

* `AddChannel` (`0x05d74c`) consumes **`17.33%`** of the `40M` instruction budget (`48,017 / 277,144` samples = `~6.9M` instructions/s).
* `_Z8UpdateFSv` (`0x00b838`) gets **`31.11%`** (`86,232` samples) and `_Z7CKBlittiiP7SURFACEiiii` (`0x00ce04`) gets **`12.88%`** (`35,708` samples).
* The game therefore achieves **`12–15` framebuffer changes/s (`8–12` complete game frames/s)** and feels responsive at the start of the level.

At **modeled second `38`**, the full multi-channel `.XM` arrangement and gameplay sound effects kick in:

* Because the SDL audio thread wakes up `172.3` times per second (`64`-sample blocks at `11,025 Hz`) to feed `/dev/dsp`, its workload scales with the number of active `.XM` + SFX channels regardless of the frame rate.
* Across `Seconds 47..60` at Ratio 2, `AddChannel` (`0x05d74c`) surges from `6.9M` instructions/s to **`25.2M` instructions/s (`352,376 / 555,005` samples = `63.49%` of the entire `40M` CPU budget)**.
* With `63.49%` of the CPU consumed by `AddChannel` and another `~12%` by kernel `NWFPE` float traps + `/dev/dsp` syscalls, the main game thread is starved to `< 25%` of the `40M` budget.

### 3. The `_Z12UpdateTimersv` (`0x01d4b8`) Catch-Up Spiral

In `_Z9UpdateMapv` (`0x01d6b4..0x01d744`), the game advances actor physics in a catch-up loop that executes `_Z12UpdateActorsv()` and `_Z11GAME_Updatev()` `frameTicks` times per rendered frame (`frameTicks = (now - lastTimer) >> 2`). Every physics tick executes FPA11 floating-point collision checks (`_Z9TestFloorP5ACTORiii` at `0x00f80c`, `_Z11TestCeilingP5ACTORiii` at `0x00fb2c`, `_Z12TestLeftWallP5ACTORiii` at `0x00fe5c`, `_Z13TestRightWallP5ACTORiii` at `0x010188`), each trapping into `NWFPE`.

As `AddChannel` starves the main thread, frame duration grows toward `128 ms` (`frameTicks = 32`), forcing each frame to run up to 32 physics + `NWFPE` iterations before drawing a single frame. Once `frameTicks > 32` (`0x01d4e0: cmp r3, #0x20; bhi 0x01d508`), `_Z12UpdateTimersv` discards elapsed wall time and substitutes `lastTicks`, locking the game into a **`2.4 FPS` (`6.9 FB_Chg/s`) slow-motion collapse**.

---

## Eliminating the Guest `NWFPE` Bottleneck via Direct FPA11 Hardware Execution

While raising `cpuStepsPerTick` to `4` (`80M` steps/s) worked around the guest bottleneck in headless probes, a `4x` preset requires **`80.0 MIPS`** of sustained host throughput for `100%` real-time speed. In live embedded launcher testing (`oceanblast.exe --parent-hwnd`), Windows 11 background power throttling (`EcoQoS`) and per-batch `DirectInput8` controller polling (`800–1,600` calls/s) capped host throughput at `32–43 MIPS` (`~48%` real-time speed), causing 18-second wall-clock loading times, `424` audio queue underrun events, and half-speed audio/animations.

To solve both the guest instruction explosion and the host MIPS requirement at their root:

1. **Direct Hardware Execution of ARM FPA11 (`cp1` / `cp2`) Floating-Point Instructions (`src/cpu/arm920t.cpp`, `src/cpu/arm920t.h`):**
   - Implemented full ARM FPA11 coprocessor execution directly in `ARM920T`:
     - `executeFPA11DataTransfer`: `cp1` `LDFS`/`STFS` (32-bit IEEE-754 single) and `LDFD`/`STFD` (64-bit FPA mixed-endian double: high word at `addr`, low word at `addr + 4`), plus `cp2` `LFM`/`SFM` (1–4 registers, 12-byte tag/high/low frames) with clean `handleDataAbort` page-fault recovery so Linux demand-paging and Copy-On-Write work transparently on floating-point loads and stores.
     - `executeFPA11Operation`: `CPRT cp1` (`FLT`, `FIX` with nearest/ `+inf` / `-inf` / zero rounding modes, `WFS`, `RFS`, `WFC`, `RFC`, and `CMF`/`CMNF`/`CMFE`/`CMNFE` flag comparisons against registers and FPA11's constant table `{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 0.5, 10.0}`) and `CPDO cp1` binary/unary operations (`ADF`, `MUF`, `SUF`, `RSF`, `DVF`, `RDF`, `POW`, `RPW`, `RMF`, `FML`, `FDV`, `FRD`, `MVF`, `MNF`, `ABS`, `RND`, `SQT`, `LOG`, `LGN`, `EXP`, `SIN`, `COS`, `TAN`, `ASN`, `ACS`, `ATN`, `URD`, `NRM`).
   - Executing FPA11 instructions in hardware eliminates **100% of the `NWFPE` Undefined Instruction trap storm (`362M` instructions during `InitAudio`)** and stops `switchMode(UND/SVC/USR)` from flushing `Bus::instructionPage` millions of times per second.
   - Controlled via `--fpa11 <on|off>` (default `on`).

2. **Full-System Workload Comparison: Ratio 2 (`No FPA11`) vs Ratio 2 (`FPA11`) vs Ratio 3 (`FPA11`):**

| Metric | Ratio 2 (`40M`/s, No FPA11) | Ratio 2 (`40M`/s, FPA11) | Ratio 3 (`60M`/s, FPA11) | Improvement (Ratio 3 + FPA11) |
| --- | ---: | ---: | ---: | --- |
| Kernel `NWFPE` CPU share during `InitAudio` | `65.33%` (`362,598` samples) | `0.00%` (`0` samples) | `0.00%` (`0` samples) | **100% eliminated** |
| Frozen Arcade Lab loading duration (`InitAudio`) | `14 s` (`Seconds 9..22`) | `3 s` (`Seconds 10..12`) | `3 s` (`Seconds 7..9`) | **`4.67x` faster (`-11 s`)** |
| Total modeled time from power-on to Title Screen | `23 s` | `13 s` | `10 s` | **`2.30x` faster (`-13 s`)** |
| Title Screen `User%` / main-loop `select` rate | `60.7%` / `28–36 /s` | `92.8%` / `37–49 /s` | `94.7%` / `86–103 /s` | **`+2.8x` Title loop rate** |
| Gameplay `User%` (`Seconds 20..60`) | `78.8%` | `93.4%` | `95.6%` | Kernel overhead reduced to `4.4%` |
| Late-level `AddChannel` (`0x05d74c`) CPU share | `63.49%` | `63.00%` | `42.46%` | Leaves `57.5%` CPU for game thread |
| Late-level `_Z8UpdateFSv` (`0x00b838`) CPU share | `8.22%` | `13.11%` | `26.98%` | **`4.92x` more downsampling work/s** |
| Late-level framebuffer changes/s (`FB_Chg`, `Seconds 38..60`) | `6.87 /s` (`5..8 /s`) | `10.91 /s` (`8..14 /s`) | `19.09 /s` (`15..25 /s`) | **No slow-motion collapse** |
| Host MIPS required for `100%` real-time speed | `40.0 MIPS` | `40.0 MIPS` | `60.0 MIPS` | **`-25%` lower target than `4x`** |

Byte-verified ELF function attribution and live GUI performance files are stored in:

* [`docs/validation/2026-10-11_chefs_ratio2_loading_8_22_functions.json`](validation/2026-10-11_chefs_ratio2_loading_8_22_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio2_early_game_31_37_functions.json`](validation/2026-10-11_chefs_ratio2_early_game_31_37_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio2_late_game_38_60_functions.json`](validation/2026-10-11_chefs_ratio2_late_game_38_60_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_loading_5_10_functions.json`](validation/2026-10-11_chefs_ratio4_loading_5_10_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_game_20_60_functions.json`](validation/2026-10-11_chefs_ratio4_game_20_60_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_late_game_38_60_functions.json`](validation/2026-10-11_chefs_ratio4_late_game_38_60_functions.json)
* [`docs/validation/2026-10-11_chefs_fpa11_ratio3_gui_performance.csv`](validation/2026-10-11_chefs_fpa11_ratio3_gui_performance.csv)

---

## Fixes Implemented and Verified Live GUI + Sound Performance

1. **ARM FPA11 Coprocessor Execution & ALU Fast Path (`src/cpu/arm920t.h`, `src/cpu/arm920t.cpp`):**
   - Added hardware `cp1`/`cp2` FPA11 execution (`executeFPA11DataTransfer`, `executeFPA11Operation`) and extended `executeSimpleDataProcessing` to handle `ADC`, `SBC`, and `RSC` carry arithmetic directly inside the fast path (accelerating `libmikmod`'s 64-bit `AddChannel` loop).
   - Replaced `evaluateCondition` with a 16-entry `NZCV` bitmask lookup table, reordered `stepARM` instruction class dispatch, and replaced 16-iteration `LDM`/`STM` loops with `__builtin_popcount`/`__builtin_ctz` set-bit iteration.
2. **Automatic Cartridge Preset (`src/core/cartridge_settings.h`):**
   - Set the automatic preset for `Superstar Chefs` (`size == 17301504 && crc == 0x93209877u`) to `cpuStepsPerTick = 3` (`60M` instructions per modeled second).
3. **Host Build Inlining & Profile-Guided Optimization (`Makefile`, `tools/build_profiled.ps1`):**
   - Ordered `bus.cpp` and `arm920t.cpp` before `main.cpp` in `build/unity.cpp` with targeted GCC inlining parameters (`--param inline-unit-growth=400 --param large-unit-insns=100000 --param max-inline-insns-single=1000 --param max-inline-insns-auto=200`), raising single-core throughput on the `400M`-instruction *Superstar Chefs* benchmark from `25.48 MIPS` (multi-TU) to **`74.09 MIPS`** (standard `make`) and **`101.61 MIPS`** (PGO unity build via `tools/build_profiled.ps1` — **`3.99x` faster**).
4. **Windows 11 Power-Throttling Prevention & Input Polling Throttle (`src/main.cpp`, `src/display/display_win32.cpp`):**
   - Disabled Windows 11 `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` (`EcoQoS`) via `SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, ...)` and raised process/thread priority to `ABOVE_NORMAL_PRIORITY_CLASS` so embedded `--parent-hwnd` child processes are never throttled to base/idle clock speeds.
   - Gated `Display::processEvents()` `PeekMessageA` calls with `GetTickCount64()` / `HIWORD(GetQueueStatus(QS_ALLINPUT))` and throttled `InputManager::poll()` (`DirectInput8` + `XInput`) to `125 Hz` (`8 ms`).
5. **Live 60-Second `--gui --sound` Verification ([`2026-10-11_chefs_fpa11_ratio3_gui_performance.csv`](validation/2026-10-11_chefs_fpa11_ratio3_gui_performance.csv)):**
   - Across 60 full seconds of live GUI + audio execution (`3,541,900,000` instructions, `1,249,280` stereo audio frames at `21,972 Hz`):
     - **Real-time speed (`speed_percent`):** **`100.00%` average** (`98.80%..101.22%`, `60.00 MIPS`).
     - **Arcade Lab loading screen:** Appears at `6.0 s`, finishes `InitAudio` in **`3.0 s`**, and reaches the animated Title Screen + music at **`10.0 s`** (`25.0–32.0` presented FPS).
     - **Audio stability:** **`0` dropped samples**, **`0` empty queue events across the first 52 seconds** (`1` event total in 60 seconds vs `424` previously), with `2,048–3,072` queued frames maintained continuously.
     - **Late-level gameplay (`Seconds 38..60`):** Sustains **`15.0–22.0` presented FPS** (`avg 16.4 FPS`) at `100.0%` real-time speed without slowdown.

