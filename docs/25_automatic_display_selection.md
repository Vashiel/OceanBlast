# Automatic Display Selection

## Display Initialization and Mode Transitions

The preloaded NAND splash contains packed RGB444 pixels with 360-byte rows. The first GUI update previously used the renderer's default RGB565 interpretation, producing a malformed initial image before LCD setup. Initialization now explicitly decodes that splash as RGB444 with its native stride.

Crazy Jack later writes packed RGB444 pixels into 480-byte Linux framebuffer rows while LCDCON1 requests RGB565. Applying a fixed RGB444/480-byte override throughout startup also changes interpretation of the native boot splash. The later RGB565 "3 Games" loading logo uses the same LCD registers and address as game output, so those fields alone cannot select the decoder. Automatic selection follows the observed startup transition as well as the current LCD configuration.

## Cartridge Compatibility Profile

The checked raw NAND dump is identified by its complete file size, 17,301,504 bytes, and IEEE CRC32 `9bce041a`. File names and local paths are not used for recognition. In automatic mode, its compatibility decoder is active only when all of these conditions match:

- Framebuffer physical address: `0x302a0000`.
- LCDCON1: `0x579`.
- Register-derived row stride: 480 bytes.
- A nonempty loading image followed by a cleared active image area has been observed in that configuration.

The transition probe runs every one million executed instructions for this profile. It first observes at least 4,096 nonzero bytes in the first 360 bytes of each of the 160 padded rows. It then waits for all of those active bytes to be cleared. The remaining 120 bytes of each row retain loading-image residue and are excluded from this check. Initially empty memory cannot activate the profile. Selection remains active through dark game frames and resets when the matching LCD/address/stride configuration changes.

After the handoff, the effective layout is packed RGB444 with 480-byte rows; only the first 360 bytes of each row contain the decoded 240 pixels. Before the handoff, the RGB565 loading logo follows the registers. Other configurations and unrecognized cartridges follow the existing LCD register decoder. Returning to a native splash configuration restores its native layout. No pixel movement, guessed centering, guest memory patch or clock adjustment is applied.

This is a cartridge compatibility profile for the verified startup sequence, not general image-quality detection or proof of correct LCD hardware emulation. A modified startup sequence that bypasses the observed clear may require another profile or an explicit diagnostic override. CRC32 and size identify the known image for selection, not cryptographic authenticity or dump integrity. The cause of the guest register/pixel-format disagreement remains open. A separately observed horizontal offset under automatic decoding requires comparison of the active buffer and LCD address/stride at that transition; an arbitrary translation would conceal the cause.

## Controls and Diagnostics

The launcher defaults to **Automatic**. CLI `--display-format auto` selects the same behavior. `--display-format lcd` disables compatibility selection. Explicit RGB444/RGB565 and stride overrides remain available for investigation and take precedence over automatic selection. The initial preloaded splash always uses its known native format.

The startup log records cartridge CRC32 and selected profile. A transition log records the instruction count when the game layout activates or resets. Snapshot state records `display_selection`, `display_compatibility`, `cartridge_crc32`, `lcdcon1`, effective format and stride.

## Validation

All 19 ROM-free framebuffer checks pass, including the standard CRC32 vector, unknown/wrong-size image rejection, native boot rows, Crazy Jack game selection, a return to native splash, unrelated cartridges using the same registers, explicit register mode, explicit format/stride precedence, changed stride rejection, RGB565 loading output before handoff, initially empty memory, loading-image observation, padding residue, dark game frames and reset. The other nine existing regression executables also pass. The profile-guided Windows build completes without compiler warnings; training uses Crazy Jack and Superstar Chefs.

A five-billion-instruction headless replay uses `tests/crazy_play_confirm.txt` and automatic display selection without format or stride overrides. It exits normally. The compatibility handoff activates at instruction 279,000,000. Captured states and decoded images show the native digiBLAST splash at 50 million instructions, the RGB565 loading logo at 100 million, the game intro at 650 million, the title at two billion, level selection at three billion and gameplay at four billion. The 4.95-billion snapshot retains the game layout after scripted movement. [Eight-stage validation metadata](validation/2026-10-09_crazy_display_selection.csv) records effective layouts and framebuffer/decoded-image hashes; cartridge data and captures are not distributed.

An additional 400-million-instruction GUI startup run uses the installed executable, automatic display selection and `--exit-on-limit`. It exits normally and confirms the same native splash, RGB565 loading and cleared-game layouts in snapshot state. No test window remains open.

The examined native splash and game images require no added horizontal translation. This does not establish that every startup frame or another cartridge's reported offset is corrected. The first-window-format fix follows the known preloaded buffer layout; sustained GUI presentation, live input, performance and sound acceptance remain separate from these runs.

Reproduction:

```sh
bin/oceanblast.exe "roms/games/Crazy Jack [G] (EN).bin" \
  --steps 5000000000 --snapshot-interval 50000000 \
  --input-script tests/crazy_play_confirm.txt --display-format auto
```

The audit tool defaults to automatic display selection and explicitly passes the selected mode, including `lcd` when register-only comparison is requested. The image audit renderer reads each snapshot's effective format and stride; final-buffer rendering uses the reported decoder in the session log. Buffer byte count alone cannot distinguish padded RGB444 from RGB565.
