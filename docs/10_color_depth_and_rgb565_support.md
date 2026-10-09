# S3C2410 Color Depth Resolution & 16-bit RGB565 Display Support

Date: 2026-10-07. Baseline: `2d66c3db`.
Reported symptom: *Crazy Jack [G] (EN).bin* boots and runs, but the splashscreen and in-game visuals display severe graphics glitches, horizontal shearing, and pixel artifacts across the entire screen.

---

## 1. Root Cause Analysis

### A. S3C2410 LCD Controller Dual-Mode Architecture
The Samsung S3C2410 application processor's LCD controller supports multiple color depths and panel modes configured via `LCDCON1` (`0x4D000000`), `LCDCON5` (`0x4D000010`), and `LCDSADDR1..3`:
1. **Bootloader Stage (U-Boot):**
   - Configures STN 12-bit packed LCD444 mode (`LCDCON1 = 0x14C9`, `BPPMODE = 0b0100`).
   - Framebuffer size: $240 \times 160 \times 1.5 = \mathbf{57\,600 \text{ bytes}}$.
   - `LCDSADDR1 = 0x18180000` (PA `0x30300000`), `LCDSADDR2 = 0x18187080` (Span `0x7080` = 57,600 bytes).
   - Used for the U-Boot console logo and cartridge bootsplash images.

2. **Operating System & Game Runtime (Linux `s3c2410fb`):**
   - Switches the LCD controller to **TFT 16-bit RGB565** true-color mode (`LCDCON1 = 0x579`, `BPPMODE = 0b1100`).
   - `LCDCON5 = 0x801` (`FRM565 = 1` for 5:6:5 color format, `HWSWP = 1` for half-word endianness alignment).
   - Framebuffer size: $240 \times 160 \times 2 = \mathbf{76\,800 \text{ bytes}}$.
   - `LCDSADDR1 = 0x18150000` (PA `0x302A0000`), `LCDSADDR2 = 0x18159600` (Span `0x9600` = 76,800 bytes).
   - The register configuration requests RGB565 output. The actual userspace buffer layout must be checked separately; Crazy Jack is an observed exception, described below.

### B. Framebuffer Format Mismatch
Prior to this fix, the emulator's Win32 display pipeline unconditionally assumed that all display buffers were 12-bit packed LCD444 (57,600 bytes).

When reading a 16-bit RGB565 framebuffer (76,800 bytes) with a 12-bit decoder:
1. **Stride & Pixel Desynchronization:** Every 3 bytes were interpreted as 2 pixels, whereas in 16bpp every 4 bytes represent 2 pixels. Every scanline suffered progressive horizontal phase displacement ($1/3$ pixel offset per pixel pair).
2. **Channel Scrambling:** Nibble-unpacking 16-bit little-endian integer words scrambled the red, green, and blue bit positions, generating noisy pixel patterns and colored horizontal stripes.
3. **Vertical Truncation:** Because only 57,600 bytes out of 76,800 bytes were read, the bottom 40 scanlines ($160 - 57600 / 480 = 40$) were missing or corrupted.

---

## 2. Technical Implementation

### 1. Dynamic Hardware BPP Mode Detection (`src/memory/bus.h`, `src/memory/bus.cpp`)
Implemented `Bus::isLcd16Bpp()` and `Bus::getFramebufferSize()` to inspect the S3C2410 LCD controller registers:
* `LCDCON1` bits [4:1] (`BPPMODE`):
  * `0b1100` (12) -> TFT 16bpp (`true`, 76,800 bytes).
  * `0b0100` (4) or `0b1000` (8) -> STN / TFT 12bpp (`false`, 57,600 bytes).
* `LCDSADDR1` / `LCDSADDR2`: Framebuffer span check ($\ge 76\,800 \text{ bytes}$ -> 16bpp).
* Default fallback: 16bpp when the MMU is active (Linux kernel running), 12bpp during initial boot.

### 2. High-Performance Dual-Mode Pixel Rendering (`src/display/display_win32.cpp`, `src/display/display.h`)
Updated `Display::updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr, bool is16bpp)`:
* **16-bit RGB565 Mode:** Decodes 16-bit little-endian words directly into 32-bit XRGB using branchless bit-replication:
  ```cpp
  uint16_t w = static_cast<uint16_t>(src[i * 2]) | (static_cast<uint16_t>(src[i * 2 + 1]) << 8);
  uint32_t r = (w >> 11) & 0x1F;
  uint32_t g = (w >> 5) & 0x3F;
  uint32_t b = w & 0x1F;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  m_pixels[i] = (r << 16) | (g << 8) | b;
  ```
* **12-bit Packed LCD444 Mode:** Retained 3-byte-to-2-pixel nibble unpacking for U-Boot bootsplashes and 12bpp game titles (*Pitfall*).

### 3. Subsystem Integration (`src/main.cpp`)
* **Display Frame Pacing & Hashing:** Frame change detection hashes 76,800 bytes for 16bpp or 57,600 bytes for 12bpp.
* **F7 State Snapshots:** Dumps authentic raw framebuffers with matching `.raw` file size and records the active format in `snapshot_<step>.txt`.
* **Diagnostic Dumps:** Reports active framebuffer resolution, nonzero bytes, and detected mode on exit.

---

## 3. Verification & Results

1. **Test Suite:** `make test` passes **22 / 22 PASS (0 failures)**.
2. **Pitfall The Lost Expedition:** Correctly recognized as `12bpp packed` (57,600 bytes).
3. **Crazy Jack:** LCD registers request `16bpp RGB565` (76,800 bytes), but this does not establish the application pixel layout. EEPROM support restores startup; correct host decoding currently requires packed RGB444 with a 480-byte row stride. See [EEPROM and framebuffer validation](16_i2c_eeprom_and_player_startup.md).

## Crazy Jack Follow-up

The LCD register mode alone does not establish the format of the pixels written by Crazy Jack. [EEPROM and framebuffer validation](16_i2c_eeprom_and_player_startup.md) records correct RGB444 decoding with 480-byte scanlines despite RGB565 register configuration. [Automatic display selection](25_automatic_display_selection.md) now follows the checked cartridge's loading-to-game transition; the underlying register disagreement remains unresolved.
