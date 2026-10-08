# UART0 Transmit Interrupts & Cartridge Startup Compatibility

Date: 2026-10-08. Baseline: `579131f`.

## 1. Root Cause

UART0 reported an empty transmit buffer through `UTRSTAT0`, and writes to `UTXH0` reached the host console. However, the emulator never generated the corresponding transmit interrupt. Polled U-Boot and kernel console output could work while Linux's interrupt-driven TTY path eventually blocked guest applications.

The Italian/Spanish combined cartridges stopped after the language application's theme-loading output. The saved guest kernel context was waiting inside a write path; no further userspace return or syscall appeared. Supplying the missing UART transmit interrupt lets these applications finish the write and display their language menus. The same correction restores visible Superstar Chefs gameplay output.

This is a peripheral interrupt defect, separate from the invalid executable data in the international Winx combined dump described in [the ROM integrity report](14_rom_integrity.md).

## 2. Register Behavior

| Register / field | Modeled behavior |
| --- | --- |
| `UCON0` at `0x50000004`, bits [3:2] | Transmit IRQ generation requires interrupt mode (`01`); DMA mode does not generate TX IRQs |
| `UCON0` bit 9 | Empty level condition reasserts the transmit source; edge mode relatches on completion/enable transitions |
| `SUBSRCPND` at `0x4a000018`, bit 1 | UART0 TX pending condition, including when sub-masked |
| `INTSUBMSK` at `0x4a00001c`, bit 1 | Controls propagation of TXD0 to the main controller |
| Main interrupt bit 28 | UART0 parent interrupt, subject to `INTMSK` |
| `UTXH0` at `0x50000020` | Byte transmission to the existing host sink followed by completion signaling |
| `URXH0` at `0x50000024` | Receive register; writes no longer incorrectly transmit characters |

The Linux [S3C2410 serial register definitions](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/arch-s3c2410/regs-serial.h) provide the register encodings. The implementation is original emulator code.

The current sink consumes each byte immediately, so its transmit FIFO remains empty. This is sufficient for the guest driver's transmit/wakeup path, but does not implement a baud-rate-timed FIFO, incoming serial data or complete UART1/2 behavior.

Eleven ROM-free regression checks cover source latching, both interrupt masks, unmasking a pending source, level reassertion after acknowledgement, edge behavior, byte writes and absence of TX IRQs in DMA mode. `make test` also passes the existing CPU/DMA/input and streaming-resampler suites.

## 3. Verified Cartridge Changes

The combined-cartridge comparison uses 1.2 billion instructions with no injected input. Before the UART correction, both Italian/Spanish cartridges had entirely black active buffers. Both now show their language-selection menus, with 74,394 nonzero bytes in 76,800-byte RGB565 buffers. See [the combined-cartridge results](validation/2026-10-08_uart_video_games.csv).

The eleven game cartridges use the same 2-billion-instruction generic input replay as the earlier compatibility audit. Superstar Chefs changes from an almost empty buffer to a visible platform-game scene. Crazy Jack remains almost empty. Gormiti Agguato still becomes black under the repeated Start/A sequence; separate single-A and single-Start experiments remain at its title with a Start prompt. The sequence alone does not identify the cause of the black transition. See [the game results](validation/2026-10-08_uart_games.csv) and [single-A results](validation/2026-10-08_gormiti_one_a.csv) and [single-Start results](validation/2026-10-08_gormiti_one_start.csv). Each single-button experiment presses at instruction 700,000,000 and releases at 720,000,000.

All nine video-only cartridges are tested with 2 billion instructions and the generic replay:

| Cartridge | Observed output | Player evidence |
| --- | --- | --- |
| Gormiti 2 episodi (IT) | Menu, then changing video images | `/helix/splay` opens `db_intro.rm`; episode playback not established |
| Gormiti Dalle Origini all'Eclissi (IT) | Menu, then changing film images | `/helix/splay` opens `VTS_01_1.rm` |
| Sonic X 1 (IT ES) | Language menu, formerly black | Movie startup unverified |
| Sponge Bob Square Pants 1 (IT ES) | Language menu, formerly black | Movie startup unverified |
| Totally Spies! 1 (IT) | Retained loading splash | Startup remains unresolved |
| Totally Spies! 1 (NL EN FR TR) | Language menu, formerly black | Movie startup unverified |
| Winx Club 1 (IT ES) | Language menu, formerly black | Movie startup unverified |
| Winx Club 1 (NL FR EN TR) | Language menu, formerly black | Movie startup unverified |
| Yu-Gi-Oh! 1 (IT ES) | Language menu, formerly black | Movie startup unverified |

The [video-only results](validation/2026-10-08_uart_video_only.csv) identify each image by SHA-256. Mount attempts on the absent/invalid `/data` partition can emit SquashFS superblock errors; these must be distinguished from the international Winx rootfs's compressed executable-page failures. Neither a nonzero buffer nor a host exit code of zero establishes complete movie/game compatibility.

## 4. Runtime Diagnostics & Remaining Validation

`--fault-log` records all exception contexts, including registers, TTB and nearby accessible instruction words. Normal demand paging and Linux's floating-point instruction emulation also generate exceptions; exception counts are not crash counts. `--trace-pc START END` records the inclusive address range before execution. Addresses accept decimal or `0x` notation. Logging can significantly reduce performance.

Snapshots include UART control and interrupt-controller state. `--exit-on-limit` closes GUI runs when their instruction budget is exhausted, enabling bounded display/audio comparisons without leaving a paused window.

The Gormiti Dalle Origini GUI/audio comparison uses 2.4 billion instructions and the default 20 MIPS limiter. Its 119 profile intervals average 19.999 MIPS; intervals after 1.5 billion instructions average approximately 2.08 sampled framebuffer changes per second and count zero dropped output samples. These are observed buffer changes, not a decoded video-frame-rate measurement. Fluid playback remains unverified; zero drops do not establish audible quality, absence of underruns or audio/video synchronization. See [the profile samples](validation/2026-10-08_gormiti_profile.csv).

The corrected Superstar Chefs GUI/audio run uses 2.4 billion instructions at the same limiter. Its 119 intervals average 19.941 MIPS; intervals after 1.2 billion instructions average 3.55 sampled framebuffer changes per second, ranging from zero to approximately 14, with zero counted dropped output samples. These samples do not establish the game's intended frame rate or audible synchronization. See [the Chefs profile samples](validation/2026-10-08_chefs_profile.csv).

A 3-billion-instruction Italian/Spanish Winx language-menu test receives Down/Up events and logs `exit current language [Italian]`, but retains the language screen and does not establish movie startup. The replay presses Down, Up, B, A, Start and Select at 1.5, 1.7, 1.9, 2.1, 2.3 and 2.5 billion instructions respectively, with each held for 20 million instructions. Key delivery reaches the application; its selection/exit path still requires tracing. See [the experiment results](validation/2026-10-08_language_buttons.csv).

Required follow-up:

1. Trace the language application after its selection/exit event, including persistent settings and child-process startup, and establish a working title-specific confirmation sequence before assessing movie playback.
2. Isolate Gormiti Agguato's first black transition with single-button tests and guest exit/read-return tracing.
3. Trace Crazy Jack's rendering and thread lifecycle; neither the UART correction nor DMA-pointer correction restores its image.
4. Separate CPU decoding cost, modeled guest time and host audio starvation during film playback. Raising only the wall-clock limiter changes peripheral timing and is not a synchronization fix.
5. Obtain independent source-data evidence for the international Winx erased page before modifying its NAND contents or attributing its launcher crash to CPU execution.

ROMs, images and full memory dumps are excluded from source publication. The published CSVs contain filenames, hashes, budgets and observations only.
