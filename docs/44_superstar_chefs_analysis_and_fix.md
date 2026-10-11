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

## Guest Workload Comparison: Ratio 2 (`40M` steps/s) vs Ratio 4 (`80M` steps/s)

Running the identical 60-modeled-second full-system probe at `cpuStepsPerTick = 4` (`80M` steps/s) with the matching input script (`tests/chefs_ratio4_confirm.txt`) demonstrates that `Ratio 4` resolves both the startup bottleneck and the mid-level collapse:

| Metric | Ratio 2 (`40M` steps/s) | Ratio 4 (`80M` steps/s) | Improvement |
| --- | ---: | ---: | --- |
| Frozen loading screen duration (`InitAudio`) | `14 s` (`Seconds 9..22`) | `5 s` (`Seconds 6..10`) | **`2.8x` faster** (`-9 s`) |
| Total modeled time to Title Screen | `23 s` | `13 s` | **`-10 s` (`43.5%` faster)** |
| Late-level `AddChannel` (`0x05d74c`) CPU share (`Seconds 47..60`) | `63.49%` (`352,376` samples) | `31.15%` (`345,803` samples) | Leaves `~69%` budget for game thread |
| Late-level `_Z8UpdateFSv` (`0x00b838`) CPU share (`Seconds 38..60`) | `8.22%` (`71,695` samples) | `29.01%` (`506,076` samples) | **`7.06x` more downsampling work completed** |
| Late-level `_Z7CKBlittiiP7SURFACEiiii` (`0x00ce04`) share (`Seconds 38..60`) | `3.26%` (`28,452` samples) | `11.42%` (`199,154` samples) | **`7.00x` more sprite/tile blitting completed** |
| Late-level main-loop frame rate (`_newselect` at `Seconds 38..60`) | `2.41 /s` (range `2..3 /s`) | `19.83 /s` (range `15..33 /s`) | **`8.23x` higher frame rate (no collapse)** |
| Late-level framebuffer changes/s (`FB_Chg` at `Seconds 38..60`) | `6.87 /s` (range `5..8 /s`) | `24.39 /s` (range `19..37 /s`) | **`3.55x` higher display update rate** |

Byte-verified ELF function attribution files for all intervals are stored in:

* [`docs/validation/2026-10-11_chefs_ratio2_loading_8_22_functions.json`](validation/2026-10-11_chefs_ratio2_loading_8_22_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio2_early_game_31_37_functions.json`](validation/2026-10-11_chefs_ratio2_early_game_31_37_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio2_late_game_38_60_functions.json`](validation/2026-10-11_chefs_ratio2_late_game_38_60_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_loading_5_10_functions.json`](validation/2026-10-11_chefs_ratio4_loading_5_10_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_game_20_60_functions.json`](validation/2026-10-11_chefs_ratio4_game_20_60_functions.json)
* [`docs/validation/2026-10-11_chefs_ratio4_late_game_38_60_functions.json`](validation/2026-10-11_chefs_ratio4_late_game_38_60_functions.json)

---

## Fixes Implemented

1. **Automatic Cartridge Preset (`src/core/cartridge_settings.h`):**
   - Updated the automatic preset for `Superstar Chefs` (`size == 17301504 && crc == 0x93209877u`) from `cpuStepsPerTick = 2` to `cpuStepsPerTick = 4` (`80M` instructions per modeled second), and updated [`tests/cartridge_settings.cpp`](../tests/cartridge_settings.cpp).
2. **ARM920T Interpreter Hot-Path Optimizations (`src/cpu/arm920t.cpp`):**
   - Replaced the multi-branch `evaluateCondition` switch with a branchless 16-entry `NZCV` bitmask lookup table (`kConditionMask[cond] >> (cpsr >> 28)`).
   - Restructured `ARM920T::stepARM` so `LDR`/`STR`, `LDM`/`STM`, and `B`/`BL` bypass the `executeSimpleDataProcessing` pre-check, and partitioned `case 0` on `(instr & 0x90) == 0x90` so `LDRH`/`STRH`/`LDRSH` (`executeHalfwordTransfer`) and `MUL`/`MLA`/`SMULL`/`UMULL` (`executeMultiply`) avoid sequential `BX`/`MRS`/`MSR`/`SWP` comparisons.
   - Replaced 16-iteration bit-scanning loops in `ARM920T::executeBlockDataTransfer` and Thumb `PUSH`/`POP`/`LDMIA`/`STMIA` with `__builtin_popcount` and `__builtin_ctz` set-bit iteration.
3. **Standard Build Inlining (`Makefile` & `tools/build_profiled.ps1`):**
   - Updated `Makefile` to compile `bin/oceanblast.exe` via an `-O3` unity translation unit (`build/unity.cpp`), eliminating cross-translation-unit call overhead between `runLegacyInstructions`, `ARM920T::step`, and `Bus` memory accessors in standard `make` builds (`25.48 MIPS` -> `46.08 MIPS`, `+80.8%`).
   - Updated `tools/build_profiled.ps1` to include the Windows resource object (`oceanblast_res.o`) and DirectInput/shell link libraries (`-lcomctl32 -lshell32 -lole32 -ldinput8 -ldxguid`).
