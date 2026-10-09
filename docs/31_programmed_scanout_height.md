# Programmed LCD scanout height

## Cause

The video player programs `LCDCON2 = 0x303bc605`: `LINEVAL[23:14] = 239`, selecting 240 scanout rows. The display previously decoded only 160 rows and stretched that cropped image across its full window. This omitted the bottom third of scanout and enlarged the remaining portion vertically. Nonempty framebuffer captures therefore did not establish correct video framing.

For Italian/Spanish Winx, `LCDSADDR1 = 0x18150000`, `LCDSADDR2 = 0x1815e100` and `LCDSADDR3 = 0xf0` describe a 115,200-byte span with 480-byte RGB565 rows: exactly 240 rows. Game configurations such as `LCDCON2 = 0x4f27d245` retain 160 rows. The [Linux S3C2410 framebuffer driver](https://code.googlesource.com/linux/torvalds/linux/+/91f28da8c9a054286d6917ce191349455c479478/drivers/video/fbdev/s3c2410fb.c) programs LINEVAL from `yres - 1`.

## Correction

The bus reports `(LCDCON2 >> 14 & 0x3ff) + 1` rows after configuration, retaining the initial 160-row fallback before LCDCON2 is programmed. Windows decodes all source rows, updates its top-down DIB height, and scales the entire source into the existing 240x160 presentation area. Snapshot exports, active framebuffer dumps and change detection use the same programmed height. A height transition triggers presentation even when its other layout fields are unchanged. Decoder and scene diagnostic captures also retain all scanout rows.

Source ranges are checked against SDRAM before decoding. Format and stride selection continue to use the existing LCD and cartridge layout rules.

## Validation

The regression suite passes 217 checks, including 240-row video size, the final source scanline and return to the 160-row game configuration. A separate Windows/GDI presentation test places green pixels only in source rows 160-239: the last destination row is green after full-height presentation and becomes red again when the source returns to 160 rows.

```powershell
make test
make build/display_height_win32.exe
build/display_height_win32.exe
```

A [five-billion-instruction Winx GUI replay](validation/2026-10-09_video_scanout_height.csv) at 4x closes normally and exports 115,200 bytes, covering all 240 programmed rows. Its complete SDRAM matches the previous replay, and its first 160 framebuffer rows match the previous cropped capture. The correction changes host presentation and capture extent without changing the final guest state. Snapshots record 160 rows at one and two billion instructions and 240 rows at three and four billion.

The GDI test opens and closes its own test window. This validates host cropping and height transitions; it does not establish accurate guest video scaling, playback speed, color conversion or audio synchronization. Decoder buffers and scanout buffers must be compared at matching movie timestamps before judging the player's complete scaling path. Cartridge assets and captures remain local and are not distributed.
