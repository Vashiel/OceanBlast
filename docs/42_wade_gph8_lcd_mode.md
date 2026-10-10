# Handheld 12-bpp LCD Mode via `GPH8 = 1` Across Cartridges

The [Wade executable and framebuffer driver analysis](41_wade_binary_and_s3c2410fb_analysis.md) established that `s3c2410fb_probe` (`0xc0013974..0xc00139f4`) configures GPIO pin `GPH8` (`GPHCON` `0x56000070` bits `[17:16] = 00`) as an input with its internal pull-up enabled (`GPHUDP` `0x56000078` bit 8 = `0`), then reads `GPHDAT` (`0x56000074`) bit 8 (`0x100`):
- When `GPH8 == 0`, the driver enables TV-Out mode (`use_tvout = 1`), converts the 12-bpp boot splash in-place to 16-bpp RGB565, and forces `s3c2410fb_check_var` to return `bits_per_pixel = 16` (`'PAL 240x160'`, `LCDCON1 = 0x579`, `line_length = 480` bytes).
- When `GPH8 == 1` (pulled high when no TV-Out adapter is attached), `use_tvout` remains `0`, and `s3c2410fb_check_var` selects `'LCD'` (`240×160`, `bits_per_pixel = 12`, `LCDCON1 = 0x14c9`, `line_length = 360` bytes).

This document records the input-pin model in `src/memory/bus.cpp`, the measured active-fight work reduction and function attribution in *Wade Hixton's Counter Punch*, and the resulting behavior across all 11 game cartridges and 3 combined game+video cartridges.

## GPIO `GPH8` Input-Pin Model (`src/memory/bus.cpp`)

Seeding `mmioRegs[0x56000074] = 0x100` only inside `Bus::reset()` has no effect because the guest kernel writes `0x00000010` to `GPHDAT` five times during early boot (before `s3c2410fb_probe` runs), overwriting the stored latch. On S3C2410 hardware, reading `GPHDAT` for a pin configured as an input in `GPHCON` returns the external pin level rather than the output latch.

`Bus::readMmio` (`case 0x56000074`) now checks `GPHCON` (`0x56000070`) bits `[17:16]`: when `GPH8` is configured as an input (`00`), bit 8 (`0x100`) reads high by default (modeling the internal pull-up with no TV-Out cable attached). Setting `OCEANBLAST_GPH8=0` in the environment restores the low (`use_tvout = 1`) level for diagnostic comparison.

## Driver Reply and SDL `FB_LCD444Update` Activation

In the 30-second mode replay (`600,000,000` ticks at ratio 4) with `GPH8 = 1`:
1. At `SDL_SetVideoMode(240, 160, 12, 0)`, `FBIOPUT_VSCREENINFO` (`0x4601`) returns `0` with `bits_per_pixel = 12` (`0xc`, `red = 8/4, green = 4/4, blue = 0/4`) instead of `16`.
2. On `UART0`, `libSDL-1.2.so.0.7.1` logs:
   ```text
   Checked mode 240x160 at 8 bpp, got mode 240x160 at 12 bpp
   Checked mode 240x160 at 16 bpp, got mode 240x160 at 12 bpp
   Checked mode 240x160 at 24 bpp, got mode 240x160 at 12 bpp
   Checked mode 240x160 at 32 bpp, got mode 240x160 at 12 bpp
   Using LCD444 12-bit packed support.
   ```
3. `SDL_SetVideoMode` does not allocate a shadow surface (`SDL_PublicSurface == SDL_VideoSurface`), and sets `UpdateRects = FB_LCD444Update` (`0x40129330`). Captured framebuffer images are `57,600` bytes (`240 × 160` at 12 bpp packed, `360` bytes/row).

## *Wade Hixton* Active-Fight Work Scaling and Function Attribution (`GPH8 = 1`)

The [per-second syscall records](validation/2026-10-10_wade_gph8_frame_rate.csv) cover a 460-second replay (`36.8` billion step calls at ratio 4, `80,000,000` step calls per modeled second) with `tests/wade_intro_ratio4.txt`.

### 1. Scene Timeline and Exact `89 -> 84` Fight Interval

Because the introduction and menu scenes run at the `62.5` fps (`vblTime = 16` ms) limiter (`62–63` `_newselect` calls/s and `115,000–178,000` `gettimeofday` calls/s during modeled seconds `40–265`), the scripted button sequence enters the first fight against Rocco at modeled second `267` rather than `425`.

Decoding the 12-bpp HUD captures from `frame_5320000000.raw` (`266` s) through `frame_6700000000.raw` (`335` s) establishes the exact round-counter boundaries during the active fight:
- **Modeled second `270–274`:** Round counter displays `90` (ring intro / bell).
- **Modeled second `275`:** First capture displaying **`89`**.
- **Modeled second `277`:** First capture displaying **`88`**.
- **Modeled second `278`:** First capture displaying **`87`**.
- **Modeled second `280`:** First capture displaying **`86`**.
- **Modeled second `282`:** First capture displaying **`85`**.
- **Modeled second `284`:** First capture displaying **`84`**.
- **Modeled second `304`:** Wade is knocked down at counter `80`; seconds `305–341` show the referee's 10-count (`27` fps) while the round counter remains paused at `80`, followed by return to the `62.5` fps title/menu loop at `351–460` s.

| Metric over active-fight counter `89 -> 84` (ratio 4, `80M` steps/s) | `GPH8 = 0` (16-bpp TV-Out) | `GPH8 = 1` (12-bpp LCD) | Change |
| --- | ---: | ---: | ---: |
| Modeled seconds (`89` / `84`) | `430` / `448` (`18` s) | `275` / `284` (`9` s) | **`-50.0%`** |
| Total CPU step calls (`5` counter units) | `1,440,000,000` | `720,000,000` | **`-50.0%`** |
| CPU step calls per counter unit (`64` frames) | `288,000,000` | `144,000,000` | **`-50.0%`** |
| CPU step calls per rendered frame (`320` frames) | `4,500,000` | `2,250,000` | **`-2,250,000` (`-50.0%`)** |
| `_newselect` syscalls (`1` per rendered frame) | `319` | `313` | `~320` frames |
| `gettimeofday` syscalls | `1,052` | `829` | — |
| Counter units per modeled second at ratio 4 | `0.278` (`17.8` fps) | `0.556` (`35.6` fps) | **`+100.0%`** |
| Scalar 180 MHz lower bound per counter unit (`CPI = 1`) | `1.60` s (`> 1.024` s target) | `0.80` s (`< 1.024` s target) | Fits within `1.024` s target |

Eliminating the 16-bpp TV-Out shadow surface reduces the guest work in the active fight by **2.25 million step calls per frame (`50.0%`)**, doubling the fight frame rate at ratio 4 from `17.8` fps to `35.6` fps (matching the old ratio-8 speed at ratio 4). Moreover, at `2.25` million step calls per frame (`144` million per 64-frame counter unit), the workload lies below the `2.88` million cycles/frame ceiling of a 180 MHz scalar ARM920T core at `62.5` fps (`0.80` s minimum at `CPI = 1` vs. the programmed `1.024` s target).

### 2. Byte-Verified Function Attribution (`275..284` s)

Running `tools/attribute_guest_functions.py` over the active fight interval (`275..284` s, `713,577` total PC samples) produces the [12-bpp SDL attribution](validation/2026-10-10_wade_gph8_sdl_functions.json) (`596,078 / 596,078` samples byte-verified `identical`) and the [12-bpp `Wade` executable attribution](validation/2026-10-10_wade_gph8_executable_functions.json) (`66,511 / 66,511` samples byte-verified `identical`):

| Binary | Function | Guest entry | `GPH8 = 0` (`430..450` s) | `GPH8 = 1` (`275..284` s) |
| --- | --- | --- | ---: | ---: |
| `libSDL-1.2.so.0.7.1` | `BlitNtoN` (`RGB444 -> RGB565` shadow blit) | `0x401151e0` | `51.33%` (`813,957`) | **`0.00%` (`0`)** |
| `libSDL-1.2.so.0.7.1` | `BlitNtoNKey` (`BGR444 -> RGB444` keyed blit) | `0x40117bf8` | `31.27%` (`495,879`) | `59.76%` (`426,464`) |
| `libSDL-1.2.so.0.7.1` | `Blit2to2Key` (`RGB444 -> RGB444` HUD blit) | `0x40117a80` | `6.44%` (`102,194`) | `12.58%` (`89,785`) |
| `libSDL-1.2.so.0.7.1` | `FB_LCD444Update` (`12-bpp` packing to `/dev/fb0`) | `0x40129330` | `0.00%` (`0`) | **`10.08%` (`71,900`)** |
| `libSDL-1.2.so.0.7.1` | `SDL_MixAudio` | `0x401034c4` | `0.33%` (`5,179`) | `0.61%` (`4,335`) |
| `Wade` | `decompressFrame` (`_Z15decompressFramePtS_S_P11SDL_Surfacebi`) | `0x0001b760` | `4.33%` (`68,595`) | `8.24%` (`58,777`) |
| `Wade` | `decompressRLEData` (`_Z17decompressRLEDataPtS_`) | `0x0001caf4` | `0.12%` (`1,870`) | `0.21%` (`1,531`) |
| `Wade` | `convertSprite` (`_Z13convertSpritePhPtii`) | `0x0002868c` | `0.11%` (`1,767`) | `0.20%` (`1,440`) |

In the 12-bpp LCD path, `BlitNtoN` drops from `51.33%` (`813,957` samples) to **zero**, confirming that `BlitNtoN` was invoked exclusively for the `RGB444 -> RGB565` shadow-surface conversion during `SDL_UpdateRect`. All GBA background and sprite blits inside `Wade` carry a source color key (`0x0f0f`) and therefore use `BlitNtoNKey`.

## 14-Cartridge Comparison (`GPH8 = 0` vs. `GPH8 = 1`)

The [14-cartridge audit](validation/2026-10-10_gph8_cartridge_audit.csv) runs all 11 commercial game cartridges and all 3 combined video+game cartridges for `1,500,000,000` instructions under both `GPH8 = 0` (`OCEANBLAST_GPH8=0`) and `GPH8 = 1` (default):

| Cartridge | `GPH8 = 0` active FB bytes | `GPH8 = 1` active FB bytes | `GPH8 = 1` `LCDCON1` / `LCDSADDR3` | Result |
| --- | --- | --- | --- | --- |
| *Crazy Jack* `[G] (EN)` | `54,324 / 76,800` | **`54,041 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Cuccioli Cerca Amici* `[G] (IT)` | `57,097 / 57,600` | `57,097 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *DigiQUAD* `[G] (EN)` | `68,558 / 76,800` | **`51,135 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Gormiti: Agguato nella Valle* `[G] (IT)` | `57,549 / 57,600` | `57,549 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Gormiti: Lotta Oscura* `[G] (IT)` | `57,600 / 57,600` | `57,600 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Gormiti: Masters of the Gorm Island* `[G] (IT)` | `57,394 / 57,600` | `57,394 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Pitfall: The Lost Expedition* `[G] (EN)` | `49,713 / 57,600` | `49,713 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Rayman 3* `[G] (M10)` | `57,442 / 57,600` | `57,442 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Spider-Man: Mysterio's Menace* `[G] (EN)` | `35,445 / 57,600` | `35,445 / 57,600` | `0x14c9` / `0xb4` (`360` B) | Unchanged (12-bpp LCD444) |
| *Superstar Chefs* `[G] (EN)` | `76,800 / 76,800` | **`57,581 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Wade Hixton's Counter Punch* `[G] (EN)` | `70,310 / 76,800` | **`52,256 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Sponge Bob + 5 Atari Games* `[V+G] (IT ES)` | `74,394 / 76,800` | **`56,534 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Winx Club + 5 Atari Games* `[V+G] (IT ES)` | `74,394 / 76,800` | **`56,534 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |
| *Winx Club + 5 Atari Games* `[V+G] (NL FR DE TR)` | `61,369 / 76,800` | **`46,301 / 57,600`** | `0x14c9` / `0xb4` (`360` B) | Switches to native 12-bpp LCD444 |

All 14 cartridges complete without kernel panics, segmentation faults, or timeouts.

### Resolution of the *Crazy Jack* Register/Format Disagreement (`docs/25`)

In [Automatic Display Selection](25_automatic_display_selection.md), *Crazy Jack* wrote packed 12-bpp RGB444 pixels into 480-byte scanlines while `LCDCON1` was `0x579` (16-bpp RGB565), requiring a special `CrazyJackDisplayTransition` compatibility profile (`display_compatibility = 1`) and causing boot-splash artifacts when `s3c2410fb_probe` converted the 12-bpp splash in-place to 16-bpp RGB565.

With `GPH8 = 1`, *Crazy Jack*'s kernel remains in handheld `'LCD'` mode (`LCDCON1 = 0x14c9`, `LCDSADDR3 = 0xb4`, `fix.line_length = 360` bytes). Because *Crazy Jack* queries `fix.line_length` from `/dev/fb0` for its row stride while always rendering 12-bpp packed RGB444 pixels, both the kernel LCD registers (`12 bpp`, `360` bytes/row) and the game's framebuffer writes (`12 bpp`, `360` bytes/row) now agree throughout the entire 4-billion-instruction replay (`tests/crazy_play_confirm.txt`):
- `50M` and `100M` steps: native 12-bpp digiBLAST and `"games"` boot splashes without conversion artifacts.
- `650M`, `2000M`, `3000M`, and `4000M` steps: `int13 production` logo, title screen, level selection, and active gameplay decoded directly from the hardware LCD registers (`display_compatibility = 0`).

Similarly, a 2.2-billion-instruction replay of *Sponge Bob + 5 Atari Games* (`tests/combined_episode_confirm.txt`) with `GPH8 = 1` confirms clean 12-bpp rendering across the language selection menu (`600M`), the Atari/SpongeBob launcher (`1400M`), and episode video playback (`2000M`).

## Reproduction

```powershell
make test
make
make build/scene_work_probe.exe
build/scene_work_probe.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  9200000000 4 build/gph8-fight initial.nvram tests/wade_intro_ratio4.txt
```
