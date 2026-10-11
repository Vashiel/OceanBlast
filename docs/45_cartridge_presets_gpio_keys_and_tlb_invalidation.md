# Commercial Cartridge Presets, GPIO Input Mapping, and CP15 TLB Invalidation

## Overview

Following the hardware FPA11 coprocessor implementation and PGO unity build optimizations in [docs/44](44_superstar_chefs_analysis_and_fix.md), testing across additional commercial game cartridges (*Rayman 3*, *Pitfall: The Lost Expedition*, *Wade Hixton's Counter Punch*, *Spider-Man: Mysterio's Menace*, *Gormiti*, *Cuccioli Cerca Amici*, *Crazy Jack*, and *DigiQUAD*) exposed three cross-cartridge issues:

1. **Missing automatic CPU ratio presets:** Unlisted cartridges such as *Rayman 3* fell back to `cpuStepsPerTick = 1` (`20.0 MIPS`), starving the user-space game thread between 200-Hz Timer 4 ticks and IIS DMA audio completions.
2. **Incomplete `greykbd` GPIO / `EINT` key wiring:** Several games bound actions to `KEY_2` (`GPF5` / `EINT5`), `KEY_3` (`GPG12` / `EINT20`), `KEY_4` (`GPF1` / `EINT1`), `KEY_LEFTCTRL` (`GPG11` / `EINT19`), and `KEY_RIGHTCTRL` (`GPG8` / `EINT16`), whereas `Bus::readMmio` and `Bus::setButtonMask` previously routed `BTN_B` to `GPF1` (`KEY_4`) and omitted `GPF5` (`KEY_2`) and `GPG12` (`KEY_3`).
3. **Redundant 2,048-entry CP15 TLB flushes in Linux 2.6.11:** Kernel context switches and range invalidations issued back-to-back full TLB flushes or single-MVA invalidations (`c8, c5/c6/c7, 1`) that previously cleared all 2,048 host TLB entries each time.

---

## 1. Automatic CPU Ratio Presets for All Commercial Game Cartridges

Inspection of `src/core/cartridge_settings.h` showed that only *Pitfall* (`2x`), *Superstar Chefs* (`3x`), and *Wade Hixton's Counter Punch* (`2x`) had automatic presets. Launching `Rayman 3 [G] (M10).bin` (`17,301,504` bytes, CRC32 `0xe3e1953e`) therefore selected `Default (unrecognized cartridge)` (`cpuStepsPerTick = 1`, `20.0 MIPS`).

At `cpuStepsPerTick = 1` (`20.0 MIPS`), *Rayman 3* takes 29 modeled seconds to finish booting and suffers continuous audio underruns and single-digit frame updates because its 22,050-Hz stereo audio mixer and software tile/sprite blitter exceed a 20M-instruction-per-second allowance.

At `cpuStepsPerTick = 3` (`60.0 MIPS`):

* *Rayman 3* finishes kernel and SquashFS initialization by `12.3 s`, renders its intro at `14–24 FPS`, and animates its title screen at **`20.0–28.0 FPS`** while streaming `21,972 Hz` stereo PCM audio with **zero audio queue underruns** across the entire post-boot session (`Seconds 12.3..34.5`).
* Because `cpuStepsPerTick = 3` requires `60.0 MIPS` of host throughput (rather than `80.0 MIPS` at `4x`), the PGO unity build (`~101 MIPS` unthrottled capacity) maintains over `+65%` host throughput headroom, keeping real-time speed locked at **`100.0%`**.

`resolveCartridgeSettings()` in `src/core/cartridge_settings.h` now defines automatic presets for all 11 commercial game cartridges:

| Cartridge Title | Size (bytes) | IEEE CRC32 | Automatic Ratio (`cpuStepsPerTick`) | Target MIPS |
| --- | ---: | --- | ---: | ---: |
| *Crazy Jack* `[G] (EN)` | `17,301,504` | `0x9bce041a` | `2x` | `40.0 MIPS` |
| *Cuccioli Cerca Amici* `[G] (IT)` | `69,206,016` | `0x40a18574` | `3x` | `60.0 MIPS` |
| *DigiQUAD* `[G] (EN)` | `17,301,504` | `0xc57e4b7c` | `2x` | `40.0 MIPS` |
| *Gormiti: Agguato nella Valle* `[G] (IT)` | `34,603,008` | `0xddeec7ea` | `3x` | `60.0 MIPS` |
| *Gormiti: Lotta Oscura* `[G] (IT)` | `34,603,008` | `0xd11135f5` | `3x` | `60.0 MIPS` |
| *Gormiti: Masters of the Gorm Island* `[G] (IT)` | `17,301,504` | `0xf890f74c` | `3x` | `60.0 MIPS` |
| *Pitfall: The Lost Expedition* `[G] (EN)` | `17,301,504` | `0xd1d6118f` | `3x` | `60.0 MIPS` |
| *Rayman 3* `[G] (M10)` | `17,301,504` | `0xe3e1953e` | `3x` | `60.0 MIPS` |
| *Spider-Man: Mysterio's Menace* `[G] (EN)` | `17,301,504` | `0x142577ee` | `3x` | `60.0 MIPS` |
| *Superstar Chefs* `[G] (EN)` | `17,301,504` | `0x93209877` | `3x` | `60.0 MIPS` |
| *Wade Hixton's Counter Punch* `[G] (EN)` | `17,301,504` | `0xeba2a2c3` | `3x` | `60.0 MIPS` |

---

## 2. Disassembly of `greykbd` Input Tables Across Commercial Games

Decompressing the SquashFS images (`0x170000` / `0x18C000`) and inspecting the user-space input handlers across the commercial library established how the guest Linux `greykbd` driver (`GPF0..7` / `GPG0..15` -> Linux keycodes -> SDL keysyms) is consumed by each engine:

1. **GBA-Derived Engines (*Rayman 3* at `0x000e4314`, *Wade Hixton's Counter Punch* `Input::mapKey` at `0x0283e8`, *Pitfall: The Lost Expedition*, and *Spider-Man: Mysterio's Menace*):**
   - Bit `0x001` (`A`): `SDLK_1` (`KEY_1` <- `GPF0` / `EINT0`)
   - Bit `0x002` (`B`): `SDLK_2` (`KEY_2` <- `GPF5` / `EINT5`)
   - Bit `0x004` (`Select`): `SDLK_LCTRL` (`KEY_LEFTCTRL` <- `GPG11` / `EINT19`)
   - Bit `0x008` (`Start`): `SDLK_RCTRL` (`KEY_RIGHTCTRL` <- `GPG8` / `EINT16`)
   - Bit `0x100` (`R`): `SDLK_5` (`KEY_5` <- `GPF4` / `EINT4`)
   - Bit `0x200` (`L`): `SDLK_4` (`KEY_4` <- `GPF1` / `EINT1`)
2. **Arcade Lab Engine (*Superstar Chefs* at `0x00b468..0x00b52c`):**
   - Action 1 (`A`): `SDLK_1` (`KEY_1` <- `GPF0` / `EINT0`)
   - Action 2 (`B`): `SDLK_2` (`KEY_2` <- `GPF5` / `EINT5`)
   - Action 3 (`C`): `SDLK_3` (`KEY_3` <- `GPG12` / `EINT20`)
   - Start / Pause / Skip Cutscene: `SDLK_5` (`KEY_5` <- `GPF4` / `EINT4`)
   - Shoulder L / R: `SDLK_LCTRL` (`GPG11`) / `SDLK_RCTRL` (`GPG8`)
3. **Media Player (`realplay` / `greykbd`):**
   - Play/Pause: `KEY_P` (`GPG9` / `EINT17`)
   - Stop: `KEY_S` (`GPG10` / `EINT18`)
   - Rewind / Previous: `KEY_B` (`GPG0` / `EINT8`)
   - Fast-Forward / Next: `KEY_N` (`GPG13` / `EINT21`)

`Bus::readMmio` (`0x56000054` `GPFDAT`, `0x56000064` `GPGDAT`) and `Bus::setButtonMask` (`src/memory/bus.cpp`) now drive all corresponding active-low GPIO lines and external interrupts (`EINT0..23`):

| Emulator Button | `GPFDAT` Active-Low Bit(s) | `GPGDAT` Active-Low Bit(s) | External Interrupt(s) | Guest Keycodes Triggered |
| --- | --- | --- | --- | --- |
| `BTN_UP` / `DOWN` / `LEFT` / `RIGHT` | `GPF2` / `GPF7` / `GPF3` / `GPF6` | — | `EINT2` / `EINT7` / `EINT3` / `EINT6` | Directional pad |
| `BTN_A` | `GPF0` | — | `EINT0` (`IRQ 0`) | `KEY_1` (`SDLK_1`) |
| `BTN_B` | `GPF5` | — | `EINT5` (`IRQ 4`) | `KEY_2` (`SDLK_2`) |
| `BTN_C` | `GPF4` | `GPG12` | `EINT4` (`IRQ 4`), `EINT20` (`IRQ 5`) | `KEY_3` (`SDLK_3`) + `KEY_5` (`SDLK_5`) |
| `BTN_L` | `GPF1` | `GPG11` | `EINT1` (`IRQ 1`), `EINT19` (`IRQ 5`) | `KEY_4` (`SDLK_4`) + `KEY_LEFTCTRL` |
| `BTN_R` | `GPF4` | `GPG8` | `EINT4` (`IRQ 4`), `EINT16` (`IRQ 5`) | `KEY_5` (`SDLK_5`) + `KEY_RIGHTCTRL` |
| `BTN_START` | — | `GPG10`, `GPG8` | `EINT18`, `EINT16` (`IRQ 5`) | `KEY_S` + `KEY_RIGHTCTRL` |
| `BTN_SELECT` | — | `GPG9`, `GPG11` | `EINT17`, `EINT19` (`IRQ 5`) | `KEY_P` + `KEY_LEFTCTRL` |
| `BTN_REWIND` / `BTN_FORWARD` | — | `GPG0` / `GPG13` | `EINT8` / `EINT21` (`IRQ 5`) | `KEY_B` / `KEY_N` |

---

## 3. Fast CP15 TLB Invalidation (`flushTlbEntry` and `tlbEmpty`)

Disassembly of `mcr p15` instructions in the guest Linux 2.6.11 kernel identified two sources of unnecessary TLB flushing overhead:

1. **Back-to-back full TLB flushes:**
   - In `cpu_arm920_switch_mm` (`0xc0021be8`), `mcr p15, 0, r0, c2, c0, 0` (`Bus::setTtb`, which calls `flushTlb()`) is immediately followed at `0xc0021bec` by `mcr p15, 0, r12, c8, c7, 0` (`flushTlb()`).
   - Four kernel TLB maintenance routines (`0xc002e164`, `0xc0056218`, `0xc005637c`, `0xc005a208`) execute `mcr p15, 0, rX, c8, c6, 0` (invalidate D-TLB) immediately followed by `mcr p15, 0, rX, c8, c5, 0` (invalidate I-TLB), invoking `flushTlb()` twice in succession.
2. **Single-entry MVA invalidations treated as full 2,048-entry flushes:**
   - Eighteen kernel call sites in `v4wbi_flush_user_tlb_range` and `v4wbi_flush_kern_tlb_range` execute `mcr p15, 0, r0, c8, c6, 1` and `mcr p15, 0, r0, c8, c5, 1` (`Opcode_2 == 1`) in a 4-KB page loop (`add r0, r0, #0x1000`). Previously, `ARM920T::executeCP15` called `bus.flushTlb()` on every `crn == 8` instruction, turning an $N$-page range invalidation into $2N \times 2,048$ loop iterations.

To eliminate this overhead while keeping `Bus::TlbEntry` compact at 12 bytes (`24 KB` total for 2,048 entries, fitting within L1 data cache):

* Added `mutable bool tlbEmpty = true` to `Bus` (`src/memory/bus.h`, `src/memory/bus.cpp`). `Bus::flushTlb()` returns immediately after resetting `instructionPage = UINT32_MAX` when `tlbEmpty` is already `true`, making the second flush in every `setTtb` + `c8, c7, 0` or `c8, c6, 0` + `c8, c5, 0` pair a 1-cycle no-op.
* Added `Bus::flushTlbEntry(u32 va)` and updated `ARM920T::executeCP15` (`src/cpu/arm920t.cpp`) to invoke `bus.flushTlbEntry(val)` when `crn == 8` and `((instr >> 5) & 7) == 1` (`Opcode_2 == 1`), invalidating `tlb[(va >> 12) & (TLB_SIZE - 1)].valid = false` in $O(1)$.

---

## 4. Extended Multi-Cartridge PGO Build and Live Verification

`bin/oceanblast.exe` was built with `tools/build_profiled.ps1 -Unity -Optimization O3` using `-march=native` and 2.1-billion-instruction (`35` modeled seconds at `3x`) training runs across both `Superstar Chefs [G] (EN).bin` and `Rayman 3 [G] (M10).bin`:

* **Superstar Chefs (`3x` automatic preset, live `--gui --scale 3 --sound --profile`):**
  - Reaches the animated title screen at `10.0 s` and sustains **`100.0%` real-time speed (`60.0 MIPS`)** with **`31.0–40.0 FPS`** (`changed_fps`) and `3,072–5,120` queued audio frames.
* **Rayman 3 (`3x` automatic preset, live `--gui --scale 3 --sound --profile`):**
  - Sustains **`100.0%` real-time speed (`59.0–69.5 MIPS`)** continuously across `Seconds 12.3..34.5`, presenting **`14.0–24.0 FPS`** during the intro and **`20.0–28.0 FPS`** on the animated title screen with **`0` audio queue underruns** after boot (`6,144–7,168` queued stereo frames).

---

## 5. Multi-Language Windows Launcher and Settings UI (7 Languages)

The Windows frontend (`src/display/launcher.h`, `src/display/windows_input.h`, `src/display/input_mapping.h`) now supports 7 user-selectable interface languages under `Settings -> Folders & System`:

1. `0`: **English (Default)**
2. `1`: **Deutsch**
3. `2`: **Español**
4. `3`: **Italiano**
5. `4`: **Nederlands**
6. `5`: **Français**
7. `6`: **Türkçe**

Selecting a language immediately updates the main menu bar (`File` | `Settings`), start screen prompt, window titles, controller/keyboard binding labels, and all 6 Settings pages without requiring an application restart, and persists `[system] language=<0..6>` in `%APPDATA%\OceanBlast\windows.ini`.

