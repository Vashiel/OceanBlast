# Wade Executable and Framebuffer Driver Analysis

The preceding [SDL format and work-scaling measurements](40_wade_sdl_formats_and_work_scaling.md) established three empirical facts during the first fight against Rocco in *Wade Hixton's Counter Punch*:
1. Five round-counter units (`89 -> 84`) require `1.44` billion CPU step calls (`288` million per unit) across ratios 4, 6 and 8, whereas at ratio 16 (`320` million step calls per modeled second) the same five counter units take `5.0` modeled seconds and `790,255` `gettimeofday` syscalls.
2. In `libSDL-1.2.so.0.7.1`, `BlitNtoN` (`51.33%`) and `BlitNtoNKey` (`31.27%`) account for `82.60%` of all system PC samples at ratio 4.
3. The application calls `SDL_SetVideoMode(240, 160, 12, 0)` without `SDL_ANYFORMAT`, and the guest kernel's `FBIOPUT_VSCREENINFO` handler replies with `bits_per_pixel=16` (RGB565), causing SDL to create an RGB444 shadow surface (`0x005d3028`) in front of the RGB565 hardware surface (`0x005d26c8`).

Static and dynamic analysis of the unstripped `Wade` executable extracted from the cartridge's SquashFS image and the guest Linux 2.6.11 kernel's `s3c2410fb` driver resolves the exact code paths behind all three observations.

## Wade Executable Extraction and Function Attribution

In the OOB-stripped SquashFS image (`wade-root.sqsh` at NAND data offset `0x170000`), the `Wade` executable is stored across 99 compressed zlib data blocks from byte offset `0x2d70b1` to `0x5231a4`. Decompressing those blocks yields the complete ELF32 ARM executable:
- **Uncompressed size:** `6,365,662` bytes (`0x6121de`)
- **SHA-256:** `e92ba4574ca73b95f2feebfea42de8b3f43dd5560750c8f9ea2d606849453e88`
- **Load address:** `PT_LOAD`0 at virtual address `0x00008000` (`p_filesz = 0x5b0a20`), `PT_LOAD`1 at `0x005b8a20` (`p_filesz = 0x2000`, `p_memsz = 0x1b648`)
- **Symbol table:** Unstripped `.symtab` (section 30, `10,999` entries including `922` `STT_FUNC` symbols) and `.strtab` (section 31)

Running `tools/attribute_guest_functions.py` with `--base 0x0 --ttb 0x30fa4000` over the [ratio-4 fight interval](validation/2026-10-10_wade_executable_functions.json) (modeled seconds 430–450) and the [ratio-16 fight interval](validation/2026-10-10_wade_executable_functions_ratio16.json) (modeled seconds 247–252) attributes all executable samples with `100%` byte-for-byte identity against the final SDRAM captures (`77,658 / 77,658` at ratio 4; `68,563 / 68,563` at ratio 16):

| Function (demangled) | Symbol | Guest entry | Size (bytes) | Ratio-4 share | Ratio-16 share |
| --- | --- | --- | ---: | ---: | ---: |
| `decompressFrame(unsigned short*, unsigned short*, unsigned short*, SDL_Surface*, bool, int)` | `_Z15decompressFramePtS_S_P11SDL_Surfacebi` | `0x0001b760` | 5,012 | 4.33% | 3.41% |
| `vblank_wait()` | `_Z11vblank_waitv` | `0x00028578` | 276 | 0.0004% | 0.40% |
| `decompressRLEData(unsigned short*, unsigned short*)` | `_Z17decompressRLEDataPtS_` | `0x0001caf4` | 108 | 0.12% | 0.08% |
| `convertSprite(unsigned char*, unsigned short*, int, int)` | `_Z13convertSpritePhPtii` | `0x0002868c` | 1,636 | 0.11% | 0.13% |
| `CEsDxDisplay::DrawScreenItems()` | `_ZN12CEsDxDisplay15DrawScreenItemsEv` | `0x00029e14` | 888 | 0.04% | 0.03% |

### The `0x1b890..0x1b89c` Hotspot Inside `decompressFrame`

`decompressFrame` (`0x1b760..0x1caef`) accounts for `88.3%` of all samples inside the `Wade` executable at ratio 4. It decodes GBA-style 8×8 4-bpp background tiles (`32` bytes per tile) and tilemap entries into a 256×256 `SDL_Surface` (`pitch = 512` bytes, row advance `add lr, lr, #0x1f0`).

When a tilemap entry specifies tile index `0` (`cmp ip, #0` at `0x1b86c`), `decompressFrame` fills the 8×8 block in the destination surface with the transparent background color stored at `palette[0]` (`[r2]`) using a nested 8×8 loop (`0x1b88c..0x1b8ac`):

```arm
0x0001b884: mov  r1, #0              ; row = 0
0x0001b888: add  lr, r3, r0          ; lr = dst_row_ptr
0x0001b88c: mov  r3, #7              ; col = 7..0 (8 pixels per tile row)
0x0001b890: ldrh ip, [r2]            ; load 16-bit palette[0] color (0x0f0f)
0x0001b894: subs r3, r3, #1          ; col--
0x0001b898: strh ip, [lr], #2        ; *dst++ = palette[0]
0x0001b89c: bpl  0x1b890             ; repeat 8 times per row
0x0001b8a0: add  r1, r1, #1          ; row++
0x0001b8a4: cmp  r1, #7              ; 8 rows per tile
0x0001b8a8: add  lr, lr, #0x1f0      ; advance dst by (512 - 16) bytes to next row
0x0001b8ac: ble  0x1b88c
```

The four instructions at `0x1b890..0x1b89c` contain no function or system calls; they execute 64 times per empty 8×8 tile across up to four GBA background layers (`BG0`–`BG3`) reconstructed by `_Z14VBlankIntrWaitv` (`0x1d200`).

## Round-Counter Cadence and Frame Limiter

Disassembly of `_Z10_PlayRoundv` (`0x14060..0x148dc`), `_Z14VBlankIntrWaitv` (`0x1d200..0x1d68b`) and `_Z11vblank_waitv` (`0x28578..0x2868b`) establishes both the frame-per-unit ratio and the target frame duration.

### 1. Exact Frame-to-Counter Ratio (`64` Frames per Unit)

In `_Z10_PlayRoundv` (`0x14060`), the round timer byte at `GlobalData + 0x10` (`0x005bae2c`, rendered on screen by `_Z11_DisplayHUDv` at `0x15f5c..0x1601c`) is initialized to `90` (`0x5a` at `0x14284`) at the start of a boxing round. Inside the main round loop (`0x143d8..0x143f0`), `_PlayRound` reads `GLOBAL_nFrameCounter` (`0x005bae10`):

```arm
0x000143d8: ldr  lr, [pc, #0xcb8]    ; lr = 0x005bae10 (&GLOBAL_nFrameCounter)
0x000143dc: ldr  r1, [lr]            ; r1 = GLOBAL_nFrameCounter
0x000143e0: tst  r1, #0x3f           ; test (GLOBAL_nFrameCounter & 63) == 0
0x000143e4: bne  0x14424             ; skip decrement on 63 out of 64 frames
0x000143e8: ldrb r2, [r6, #0x10]     ; r2 = GlobalData.round_counter (0x005bae2c)
0x000143ec: sub  r4, r2, #1
0x000143f0: strb r4, [r6, #0x10]     ; GlobalData.round_counter = r2 - 1
```

`GLOBAL_nFrameCounter` (`0x005bae10`) is incremented once per frame inside `_Z14VBlankIntrWaitv` (`0x1d2ec..0x1d2f4`), immediately before calling `_Z11vblank_waitv` (`0x1d670 -> 0x28578`). Consequently:
- **Every `1` round-counter unit is unconditionally `64` rendered frames.**
- **Five counter units (`89 -> 84`) correspond to $5 \times 64 = 320$ rendered frames.**
- This matches the observed `_newselect` syscall counts across all four ratios (`319`, `322`, `326` and `313` over the 5-unit window), because `VBlankIntrWait` calls `SDL_PollEvent` / `FB_PumpEvents` once per frame.
- At `1.44` billion CPU step calls per 5 counter units (`288` million per unit), the guest executes **$288,000,000 / 64 = 4,500,000$ CPU step calls per frame**.

### 2. Target Frame Limiter in `vblank_wait()` (`16` ms per Frame)

At `0x285b4..0x285d0`, `_Z11vblank_waitv` (`0x28578`) implements a busy-wait frame limiter using `SDL_GetTicks()` (`0x0000a5bc` PLT):

```arm
0x000285b4: bl   0xa5bc              ; SDL_GetTicks()
0x000285b8: ldr  r3, [r5]            ; r3 = lastFrameTime (0x005c7600)
0x000285bc: ldr  r2, [r6]            ; r2 = vblTime (0x005b9840)
0x000285c0: rsb  r3, r3, r0          ; elapsed = now - lastFrameTime
0x000285c4: cmp  r3, r2              ; elapsed < vblTime?
0x000285c8: bcc  0x285b4             ; spin on SDL_GetTicks() until elapsed >= vblTime
0x000285cc: bl   0xa5bc              ; SDL_GetTicks()
0x000285d0: str  r0, [r5]            ; lastFrameTime = now
```

In `.data` (ELF file offset `0x5b1840`, virtual address `0x005b9840`, size `4` bytes; verified in the SDRAM captures at offset `0x01db1840`), `vblTime` is initialized to the 32-bit integer **`16` (`0x00000010`)**, and cross-referencing all literal pools in `Wade.elf` confirms that `vblTime` is never modified at runtime. Because `SDL_GetTicks()` returns integer milliseconds (`16` ms/frame = `62.5` frames/s, approximating the Game Boy Advance's `59.73` Hz vertical blanking period):
- **Programmed target duration per counter unit:** $64\text{ frames} \times 16\text{ ms/frame} = \mathbf{1,024\text{ ms}}$ (`0.9765625` counter units/s).
- **Programmed target duration for 5 counter units (`89 -> 84`):** $320\text{ frames} \times 16\text{ ms/frame} = \mathbf{5.12\text{ s}}$.

#### Sub-Jiffy `gettimeofday` Interpolation (`s3c2410_gettimeoffset`)

Although the guest kernel runs at `HZ = 200` (`5` ms per jiffy), `gettimeofday` does not step in `5` ms increments. Disassembly of `s3c2410_gettimeoffset` (`0xc00233a4..0xc00233f8`) in the guest kernel shows that every `gettimeofday` call reads Timer 4's live countdown register `TCNTO4` (`0xf0900040` -> physical `0x51000040`) and `SRCPND` (`0xf0000000` -> physical `0x4a000000`, bit 14 `INT_TIMER4`), subtracts `TCNTO4` from `timer_startval` (`0xc01b6350 = 37499 = 0x927b`), and scales the elapsed timer ticks to microseconds via `timer_usec_ticks` (`0xc01b6354 = 8738 = 0x2222`, i.e., `((elapsed * 8738) + 0x1000) >> 16`).

In `src/memory/bus.cpp` (`case 0x51000040: return timer4.observe();`) and `src/memory/timer4.h`, `TCNTO4` dynamically returns the exact sub-jiffy countdown value (`37499` down to `0` across each `5` ms period). In the ratio-16 fight interval (`247..252`), `mmio.csv` records `790,261` guest reads of `0x51000040` (`TCNTO4`) alongside `790,255` `gettimeofday` syscalls, confirming `1`-microsecond sub-jiffy resolution for `SDL_GetTicks()`'s `16` ms frame threshold.

At ratios 4, 6 and 8, a single frame consumes `4.5` million step calls, which exceeds `16` ms of modeled CPU budget (`1.28M`, `1.92M` and `2.56M` step calls per `16` ms respectively), so the loop at `0x285b4..0x285c8` never spins and `SDL_GetTicks()` is called only twice per frame (`~640` calls per 320 frames, plus audio/event timestamps). At ratio 16 (`5.12M` step calls per `16` ms), each frame finishes before `16` ms of guest time has elapsed, activating the `0x285b4..0x285c8` polling loop (`6,295` PC samples in `_Z11vblank_waitv` and `790,255` `gettimeofday` syscalls) and locking the 5-unit interval (`89 -> 84`) to `5` modeled seconds.

### 3. Unreferenced `frameskip` Calculation

Immediately after the `vblTime` loop (`0x285f4..0x28630`), `_Z11vblank_waitv` computes `clamp((now - prevTick1) / vblTime - 1, 0, 10)` and stores it to the static variable `_ZZ11vblank_waitvE9frameskip` (`0x005d0214`, alongside `_ZZ11vblank_waitvE7skipped` at `0x005d0218` and `_ZZ11vblank_waitvE11framesDrawn` at `0x005d021c`). Cross-referencing all literal pools in `Wade.elf` confirms that `0x005d0214`, `0x005d0218` and `0x005d021c` are referenced **only** inside `_Z11vblank_waitv` (`0x28678`), which writes `frameskip` and never reads it. Every call to `vblank_wait()` unconditionally renders all active OAM sprites (`0x285a4`) and tail-calls `CEsDxDisplay::UpdateScreen(false)` (`0x28630 -> 0x29d24`).

## Surface Format Mismatch Inside `Wade.elf` (BGR444 vs. RGB444)

Inspecting all callers of `SDL_SetVideoMode` and `SDL_CreateRGBSurface` in `Wade.elf` shows that the heavy reliance on `BlitNtoN` and `BlitNtoNKey` stems from two distinct format conversions:

### 1. GBA Layer and Sprite Blits onto `m_pScreen` (`BGR444 -> RGB444`)

1. `CEsDxDisplay::InitDisplay(unsigned int, unsigned int, bool, unsigned int)` (`0x29b4c`) calls `SDL_SetVideoMode(240, 160, 12, 0)` at `0x29bb0`. For a 12-bpp surface, `SDL_AllocFormat` assigns default masks:
   - `Rmask = 0x0f00`, `Gmask = 0x00f0`, `Bmask = 0x000f` (**RGB444**, recorded on `m_pScreen` at `0x005d3028`).
2. However, when `Wade.elf` converts 15-bit GBA palettes (`0b0BBBBBGGGGGRRRRR`) in `decompressFrame` (`0x1b7e8..0x1b810`) and `convertSprite` (`0x286ec..0x28714`), it places Red in bits `[3:0]`, Green in bits `[7:4]` and Blue in bits `[11:8]` (`0x0BGR`), and explicitly creates its 12-bpp surfaces with swapped Red/Blue masks:
   - `blobs_Init()` (`0x1a618..0x1a668`): creates the four 256×256 GBA background surfaces (`g_pBG0Surface`–`g_pBG3Surface` at `0x005c75d4`–`0x005c75e0`) via `SDL_CreateRGBSurface(SDL_HWSURFACE, 256, 256, 12, 0x000f, 0x00f0, 0x0f00, 0)`.
   - `convertSprite` (`0x28754`), `convert256Sprite` (`0x28db8`) and `CEsDxDisplay::CreateEmptySprite` (`0x2a8cc`): create all GBA OAM sprite and font surfaces via `SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, 12, 0x000f, 0x00f0, 0x0f00, 0)` (**BGR444**: `Rmask = 0x000f`, `Gmask = 0x00f0`, `Bmask = 0x0f00`).
3. Only the HUD bitmaps loaded from PNG files via `CEsDxDisplay::LoadBitmap` (`0x29c04`) call `SDL_DisplayFormat` (`0x29c30`), converting them once at load time to match `m_pScreen`'s `0x0f00 / 0x00f0 / 0x000f` masks. Those HUD blits therefore use SDL's fast identical-format `Blit2to2Key` (`6.44%` of samples).
4. Because the GBA background and OAM sprite surfaces use `BGR444` (`0x000f, 0x00f0, 0x0f00`) while `m_pScreen` uses `RGB444` (`0x0f00, 0x00f0, 0x000f`), every opaque background blit calls `BlitNtoN` and every transparent background or sprite blit (color key `0x0f0f`) calls `BlitNtoNKey`.

### 2. Shadow-Surface Conversion on `SDL_UpdateRect` (`RGB444 -> RGB565`)

At the end of each frame, `CEsDxDisplay::UpdateScreen(bool)` (`0x29d24`) calls `CEsDxDisplay::DrawScreenItems()` (`0x29e14`) followed by `SDL_UpdateRect(m_pScreen, 0, 0, 0, 0)` (`0x29dfc`). When SDL is running with a 16-bpp RGB565 hardware framebuffer (`0x005d26c8`) behind the 12-bpp RGB444 shadow surface (`0x005d3028`), `SDL_UpdateRect` invokes `BlitNtoN` across the full 240×160 screen (`38,400` pixels per frame) to convert `RGB444` into `RGB565`.

## Guest Kernel `s3c2410fb` Driver and `GPH8` TV-Out Detection

Disassembly of the guest Linux 2.6.11 kernel extracted from the cartridge (`uImage` at NAND data offset `0x50000`, decompressed size `0x1ac550`, loaded at `0xc0008000`) explains why `FBIOPUT_VSCREENINFO` returned `bits_per_pixel=16` instead of `12`.

### 1. Driver Mode Table (`0xc0188d14`)

The Grey Innovation `s3c2410fb` driver defines three display modes at `0xc0188d14` (stride `52` bytes):

| Index | Mode name | Resolution | `vmode` | Pixel format in `s3c2410fb_check_var` | `LCDCON1` `BPPMODE` | `line_length` |
| ---: | --- | --- | --- | --- | --- | ---: |
| 0 | `'LCD'` | 240×160 | `0x00` | `bits_per_pixel = 12` (`red = 8/4, green = 4/4, blue = 0/4`) | `12 bpp` (`0xc8`) | 360 bytes |
| 1 | `'PAL 240x160'` | 240×160 | `0x10` | `bits_per_pixel = 16` (`red = 11/5, green = 5/6, blue = 0/5`) | `16 bpp` (`0x49`) | 480 bytes |
| 2 | `'PAL 240x240'` | 240×240 | `0x10` | `bits_per_pixel = 16` (`red = 11/5, green = 5/6, blue = 0/5`) | `16 bpp` (`0x49`) | 480 bytes |

### 2. `GPH8` Sense Pin in `s3c2410fb_probe` (`0xc0013974..0xc00139f4`)

During kernel boot, `s3c2410fb_probe` (`0xc00135d4..0xc0013cb0`) checks GPIO pin **`GPH8`** via the S3C2410/OCEAN-L-20 GPIO Port H registers (`GPHCON` at `0x56000070`, `GPHDAT` at `0x56000074`, `GPHUDP` at `0x56000078` via virtual mapping `0xf0e00070`–`0xf0e00078`):

```arm
0xc0013974: ldr  r3, [r1, #0x70]     ; read GPHCON (0x56000070)
0xc0013978: bic  r3, r3, #0x30000    ; GPH8 [17:16] = 00 (Input)
0xc001397c: str  r3, [r1, #0x70]
0xc0013980: ldr  r3, [r1, #0x78]     ; read GPHUDP (0x56000078)
0xc0013984: bic  r3, r3, #0x100      ; enable pull-up on GPH8 (bit 8 = 0)
0xc0013988: str  r3, [r1, #0x78]
...
0xc00139a4: ldr  r3, [r2, #0x74]     ; read GPHDAT (0x56000074)
0xc00139a8: tst  r3, #0x100          ; test GPH8 (bit 8)
0xc00139ac: bne  0xc00139f4          ; if GPH8 == 1, skip TV-out activation
0xc00139b0: mov  r3, #1
0xc00139b4: str  r3, [r5, #0x1f8]    ; info->tvout_capable = 1 (0xc01bfdb8)
0xc00139b8: str  r3, [r5, #0x618]    ; use_tvout = 1 (0xc01c01d8)
```

- **When `GPH8 == 0` (active-low TV-Out cable sense):**
  - `s3c2410fb_probe` sets `info->tvout_capable = 1` (`0xc01bfdb8`) and `use_tvout = 1` (`0xc01c01d8`, exported at `/sys/devices/platform/s3c2410-lcd/use_tvout`).
  - At `0xc0013b00..0xc0013b88`, `s3c2410fb_probe` converts the 12-bpp packed boot splash image in the framebuffer into 16-bpp RGB565 in-place (`240 * 160` pixels).
  - In `s3c2410fb_check_var` (`0xc00be250..0xc00be3b0`), whenever `use_tvout != 0`, only modes with `vmode & 0x10` (`'PAL 240x160'` or `'PAL 240x240'`) are accepted, and the driver unconditionally overwrites `var->bits_per_pixel = 16` (`red = 11/5, green = 5/6, blue = 0/5` at `0xc00be338..0xc00be36c`).
- **When `GPH8 == 1` (pulled high when no TV-Out cable is pulling `GPH8` low):**
  - `s3c2410fb_probe` leaves `use_tvout = 0`.
  - `s3c2410fb_check_var` selects mode 0 (`'LCD'`, `vmode == 0`) and returns `var->bits_per_pixel = 12` (`red = 8/4, green = 4/4, blue = 0/4` at `0xc00be370..0xc00be394`).
  - `s3c2410fb_set_par` (`0xc00be3bc..0xc00be750`) sets `fix.line_length = (240 * 12) >> 3 = 360` bytes (`0xc00be404`) and programs `LCDCON1` to `0xc8` (12-bpp TFT mode).

### 3. Custom 12-bpp Path in `libSDL-1.2.so.0.7.1` (`FB_LCD444Update`)

In the cartridge's `libSDL-1.2.so.0.7.1`, `FB_SetVideoMode` (`0x3094c..0x30dc4`) checks the `bits_per_pixel` returned by `FBIOPUT_VSCREENINFO` at `0x30b24`:

```arm
0x00030b24: cmp  r3, #0xc            ; vinfo.bits_per_pixel == 12?
0x00030b28: beq  0x30b40
```

When the kernel returns `bits_per_pixel == 12`:
1. `FB_SetVideoMode` allocates a `240 * 160 * 2 = 76,800`-byte unpacked 12-bit buffer (`pitch = 480` bytes) directly for `SDL_VideoSurface->pixels`, sets custom surface flag `0x20000000`, and sets `this->UpdateRects = FB_LCD444Update` (`0x31330`).
2. Because `SDL_VideoSurface->format->BitsPerPixel` is `12` (`Rmask = 0x0f00, Gmask = 0x00f0, Bmask = 0x000f`), matching `Wade`'s `SDL_SetVideoMode(240, 160, 12, 0)` request, `SDL_SetVideoMode` does **not** allocate a shadow surface (`SDL_PublicSurface == SDL_VideoSurface`).
3. On each `SDL_UpdateRect`, instead of running `BlitNtoN` (`RGB444 -> RGB565`) into a shadow surface, SDL calls `FB_LCD444Update` (`0x31330..0x314a8`), which packs pairs of 16-bit `0x0RGB` pixels (`p0`, `p1`) into 3 bytes (`p0 >> 4`, `(p0 << 4) | ((p1 >> 8) & 0x0f)`, `p1 & 0xff`) in the 360-byte-per-row hardware framebuffer.

In the current emulator implementation (`src/memory/bus.cpp`), `GPFDAT` (`0x56000054 = 0x000000FF`) and `GPGDAT` (`0x56000064 = 0x0000FFFF`) are initialized as pulled high, whereas `GPHDAT` (`0x56000074`) is uninitialized and falls through `Bus::readMmio` to return `0x00000000` (`GPH8 = 0`). That causes the guest kernel to detect an active-low TV-Out signal during `s3c2410fb_probe` and boot in `use_tvout = 1` (`'PAL 240x160'`, 16-bpp RGB565) mode. No emulator behavior is modified in this document; evaluating `GPH8 = 1` (`use_tvout = 0`) and measuring steps per frame in native 12-bpp LCD mode are the next diagnostic steps.
