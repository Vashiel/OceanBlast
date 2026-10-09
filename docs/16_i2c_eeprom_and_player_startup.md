# I2C EEPROM & Media Player Startup

Date: 2026-10-08. Comparison baseline: `ffe99c3`.

## 1. Startup Blocker

The previous I2C controller completed every transaction with NACK. Kernel probes tried address bytes `0xae`, `0xa6` and `0xa0`, corresponding to EEPROM slave addresses `0x57`, `0x53` and `0x50`. No EEPROM was attached. The guest HAL fell back to `/tmp/nvram.bin`; initialization in subsequent processes lost the selected language. This prevented combined and video-only launchers from completing their normal handoff. Crazy Jack also failed before its title sequence.

The implementation now supplies a separate, initially erased 2048-byte EEPROM and original transaction handling. Cartridge bytes are unchanged. No factory language, application-specific return value or executable patch is injected. The capacity follows the guest's supported 24C16 probe path; the physical EEPROM capacity of every hardware revision has not been verified.

## 2. Controller and Storage Behavior

| Component | Implemented behavior |
| --- | --- |
| EEPROM address | Eight 256-byte banks at 7-bit addresses `0x50` through `0x57` |
| Word address | One-byte address within the selected bank |
| Writes | Sixteen-byte page staging and page-local wrap; commit on modeled STOP |
| Reads | Random read through repeated START, sequential advance and 2048-byte wrap |
| IICCON | Pending-byte continuation; IRQ 27 requires interrupt-enable bit 5 |
| IICSTAT | Separate address completion, read-only ACK/NACK result, RX/TX selection |
| Receive completion | Last byte remains available when the master sends NACK |
| Reset | Resets bus protocol while preserving committed EEPROM bytes |

Protocol references: [Linux S3C2410 I2C driver](https://github.com/torvalds/linux/blob/v2.6.12/drivers/i2c/busses/i2c-s3c2410.c) and [Microchip 24LC16B family datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/20001703M.pdf). The model uses a fixed 50-instruction transfer delay, not a clock-derived I2C waveform. STOP timing is simplified, and EEPROM write-cycle busy polling is not modeled. Unrelated slaves, including the codec address, still NACK; this change does not implement codec control or solve audio synchronization.

`--nvram PATH` optionally loads and saves exactly 2048 bytes. A missing file starts erased; an existing invalid-size or unreadable file rejects startup. Saving occurs on normal exit, and the parent directory must exist. Without this option, storage persists across guest processes and bus resets within the running instance, but not across separate emulator launches. The launcher exposes this through **Keep Device Settings**, using a shared `bin/sessions/board.nvram` image for the emulated device. Keep the EEPROM image separate from cartridge files.

## 3. Crazy Jack: Startup Restored, Display Selection Open

The manual decoder observations below describe the preceding implementation. [Automatic display selection](25_automatic_display_selection.md) now handles the checked Crazy Jack image's boot/loading/game transitions. The underlying LCD-register disagreement remains unresolved.

A paired no-input test of the same Crazy Jack image runs for two billion instructions. The previous build ends with only 283 nonzero bytes in its 76,800-byte active buffer. With EEPROM support, the buffer contains 54,048 nonzero bytes and the application reaches its title sequence without the previous EEPROM errors. See [before](validation/2026-10-08_crazy_eeprom_before.csv) and [after](validation/2026-10-08_crazy_eeprom_after.csv).

The LCD reports RGB565 (`LCDCON1 = 0x579`), but the title and subsequent scene decode correctly as packed RGB444 with a 480-byte row stride. Only 360 bytes per row contain the RGB444 image. Treating the whole frame as RGB565 produces stripes. The source of this disagreement remains unresolved; framebuffer activity alone does not prove the configured LCD mode is correct.

The host renderer now reads `LCDSADDR3` PAGEWIDTH/OFFSIZE as halfword counts and decodes each scanline separately. Explicit diagnostic overrides are available:

```powershell
bin/oceanblast.exe "roms/games/Crazy Jack [G] (EN).bin" --gui --sound --display-format rgb444 --display-stride 480
```

These flags affect host decoding and snapshot layout, not guest CPU execution or LCD register values. Default `--display-format lcd` follows the register-based mode. Snapshot state records the effective format, register format, stride and presence of an override.

A five-billion-instruction replay using `tests/crazy_level_confirm.txt` reaches a visible Level 1 scene with PLAY/MAIN MENU choices. [The result](validation/2026-10-08_crazy_eeprom_level.csv) explicitly records the override. This establishes scene rendering and progression beyond startup, not sustained controllable gameplay or synchronized sound.

## 4. Combined Cartridges and Video-only Results

Both Italian/Spanish combined cartridges reach actual episode images after selecting a language, moving down five launcher entries and confirming the episode. The 3.3-billion-instruction replay is in `tests/combined_episode_confirm.txt`. Winx opens `WinxClub-Ep1_audio_italian.rm`; SpongeBob opens `sbsp-Ep3_audio-italian.rm`. See [the combined episode results](validation/2026-10-08_eeprom_combined_episodes.csv). The international Winx combined dump's damaged executable data remains a separate issue; see [ROM integrity](14_rom_integrity.md).

All nine video-only cartridges were tested for 2.5 billion instructions with `tests/video_language_confirm.txt`. The player opens media on every tested cartridge; none reports a guest segmentation fault. This is a player-startup result, not a complete playback compatibility claim.

| Cartridge | Media/output observation |
| --- | --- |
| Gormiti 2 episodi | `splay` opens `db_intro.rm`; visible intro, episode playback not established |
| Gormiti Dalle Origini all'Eclissi | `splay` opens `VTS_01_1.rm`; changing episode images |
| Sonic X 1 | Player opens episode video and Italian audio; near-black final frame |
| Sponge Bob Square Pants 1 | Player opens episode media; visible water scene |
| Totally Spies! 1, Italy | Player opens episode media; near-black final frame |
| Totally Spies! 1, Netherlands | Player opens episode media; near-black final frame |
| Winx Club 1, Italian/Spanish | Player opens episode media; entirely black final frame |
| Winx Club 1, Netherlands/international | Player opens episode media; entirely black final frame |
| Yu-Gi-Oh! 1 | Player opens episode media; changing images |

[Video-only results](validation/2026-10-08_eeprom_video_only.csv) include image identities and instruction budgets. Both video-only Winx variants remain at zero nonzero bytes after an extended 6.5-billion-instruction run; [extended results](validation/2026-10-08_eeprom_winx_long.csv). Increasing the budget alone has not restored output. Both logs reach the playing state (guest timestamps 133 and 138 seconds), so player creation and the initial pause/seek handshake are not sufficient explanations. Decoded-frame submission, subsequent thread wakeups and audio-clock interaction remain investigation targets. The video-only Winx variants are distinct from the known damaged international game/video combination.

The subsequent [timer and runtime validation](17_timer_and_runtime_validation.md) restores a first episode image on the Italian/Spanish video-only Winx variant, while continued playback and Netherlands output remain unresolved. Interactive Crazy Jack gameplay has also been observed with the explicit diagnostic decoder.

## 5. Reproduction and Remaining Work

Extract video ZIP archives into `extracted-video-roms` first; the audit reads `.bin` files.

```powershell
make test
python tools/audit_games.py --exe bin/oceanblast.exe --roms extracted-video-roms --output validation-video --steps 2500000000 --jobs 2 --timeout 300 --input-script tests/video_language_confirm.txt
python tools/audit_games.py --exe bin/oceanblast.exe --roms roms/games --match "Crazy Jack*" --output validation-crazy --steps 5000000000 --timeout 300 --input-script tests/crazy_level_confirm.txt --display-format rgb444 --display-stride 480
python tools/render_audit.py validation-crazy --format rgb444 --stride 480
```

ROM-free regressions cover EEPROM banking, page wrap, repeated START, reset retention, image restoration, unrelated slave NACKs, controller pending/IRQ behavior and final-byte reception. Framebuffer tests cover padded RGB444/RGB565 rows and LCD stride fields. Existing CPU/DMA, keypad, UART and streaming-resampler suites remain part of `make test`. The warning-enabled build and all suites pass. CLI checks verify save/reload of a 2048-byte image, rejection without modification of an existing short image, and invalid display arguments.

Remaining acceptance work: explain Crazy Jack's register/pixel-format disagreement; restore black media-player output; verify sustained input-controlled gameplay, pitch and audio/video synchronization on each title. The alternative MAME Italian/Spanish Winx set is not present locally, so an independent comparison has not been performed. Published CSVs contain reproducible cartridge identities and observations; ROM contents, extracted programs and framebuffer captures are not included.
