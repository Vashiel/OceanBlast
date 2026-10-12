# Changelog & Technical Investigation History

This changelog records the chronological development, hardware reverse-engineering milestones, and empirical validation reports (`docs/00`–`docs/48`) for **OceanBlast**.

---

## 2026-10-12 — Runtime Build Profiles & Rayman 3 Performance

* **[Controller Settings Repaint](docs/47_controller_settings_repaint.md):** Buffered the animated controller illustration and excluded child controls from parent drawing; all 18 Windows launcher checks pass.
* **[MP3 Cartridge Firmware Probe](docs/48_mp3_cartridge_firmware_probe.md):** Identified the advertised 256-MB module and 16-MiB data image; a bounded boot reaches Linux and the splash, with an inaccessible data partition and a USB controller reset failure. MP3 playback remains unverified.
* **[Runtime Build Profiles and Rayman 3 CPU Ratio](docs/46_runtime_build_profiles_and_rayman_ratio.md):**
  * Distinguished PGO gameplay builds from standard developer builds after controlled live comparisons showed that the developer build fell below the `60 MIPS` target and accumulated audio-queue underruns in *Superstar Chefs*.
  * Separated the developer output (`build/oceanblast-dev.exe`) from the installed launcher. `tools/build_profiled.ps1 -Install` now explicitly installs the profiled executable to `bin/oceanblast.exe`.
  * Raised the automatic *Rayman 3* preset from ratio `3` to `4` after a same-host live comparison increased presented frame rate from `17.7` to `30.3 FPS` in the measured intro/title interval while sustaining `100%` emulated speed.

---

## 2026-10-11 — Commercial Cartridge Presets, GPIO Input Mapping, CP15 TLB Invalidation & FPA11 Coprocessor Execution

* **Commercial Cartridge CPU Ratio Presets, Complete `greykbd` GPIO Mapping & CP15 TLB Invalidation ([docs/45](docs/45_cartridge_presets_gpio_keys_and_tlb_invalidation.md)):**
  * Added automatic CPU ratio presets across all 11 commercial game cartridges in `src/core/cartridge_settings.h` (*Rayman 3*, *Superstar Chefs*, *Pitfall: The Lost Expedition*, *Spider-Man: Mysterio's Menace*, *Wade Hixton's Counter Punch*, *Gormiti* ×3, and *Cuccioli Cerca Amici* at `3×` / `60.0 MIPS`; *Crazy Jack* and *DigiQUAD* at `2×` / `40.0 MIPS`).
  * Verified *Rayman 3* (`CRC32 0xe3e1953e`) in live `--gui --scale 3 --sound --profile` execution: sustains **`100.0%` real-time speed (`59.0–69.5 MIPS`)**, **`20.0–28.0 FPS`** on the animated title screen, and **zero audio queue underruns** after boot.
  * Disassembled user-space SDL key tables across all 11 games and completed S3C2410 `GPFDAT`/`GPGDAT`/`EINTPEND` wiring in `src/memory/bus.cpp`: mapped `BTN_B` to `GPF5` (`EINT5` / `KEY_2`), `BTN_C` to `GPG12` (`EINT20` / `KEY_3`) + `GPF4` (`EINT4` / `KEY_5`), `BTN_L` to `GPG11` (`KEY_LEFTCTRL`) + `GPF1` (`KEY_4`), `BTN_R` to `GPG8` (`KEY_RIGHTCTRL`) + `GPF4` (`KEY_5`), `BTN_START` to `GPG10` (`KEY_S`) + `GPG8` (`KEY_RIGHTCTRL`), and `BTN_SELECT` to `GPG9` (`KEY_P`) + `GPG11` (`KEY_LEFTCTRL`).
  * Optimized ARM920T CP15 `c8` TLB invalidations (`src/memory/bus.h`, `src/memory/bus.cpp`, `src/cpu/arm920t.cpp`): added $O(1)$ single-entry MVA invalidation (`Bus::flushTlbEntry` for `Opcode_2 == 1`) and `tlbEmpty` short-circuiting for back-to-back full TLB flushes (`setTtb` + `c8, c7, 0` and `c8, c6, 0` + `c8, c5, 0`).
  * Added live 7-language localization (`English (Default)`, `Deutsch`, `Español`, `Italiano`, `Nederlands`, `Français`, `Türkçe`) across the Windows Launcher, menu bar, and all 6 Settings categories (`src/display/launcher.h`, `src/display/windows_input.h`, `src/display/input_mapping.h`), persisted under `[system] language` in `%APPDATA%\OceanBlast\windows.ini`.
* **Superstar Chefs Performance Collapse Analysis, Direct FPA11 Execution & Automatic `3×` Preset ([docs/44](docs/44_superstar_chefs_analysis_and_fix.md)):**
  * Extracted and disassembled the unstripped `SuperstarChefs.elf` (`1,278,883` bytes, `3,467` symbols) from the cartridge's SquashFS archive and ran 60-modeled-second full-system probes across `Ratio 2` (`40M` steps/s), `Ratio 3` (`60M` steps/s), and `Ratio 4` (`80M` steps/s).
  * **Frozen Loading Screen (`_Z9InitAudiov` & `NWFPE`):** Proved that `_Z13UpdateLoadingv` (`0x02a2a8`) calls `_Z9InitAudiov` (`0x022278`) on loading frame 5 to procedurally synthesize 14 sound effects in software using per-sample ARM FPA11 floating-point instructions (`AddSineTone`, `AddSawTone`, `AddTriangleTone`, `AddSquareTone`, `Tremolo`), trapping via `0xFFFF0004` into the guest Linux kernel's `NWFPE` software float emulator for **`65.33%` (`362M` instructions)** of the 14-second loading freeze at `Ratio 2`.
  * **Sudden Mid-Level Collapse (`SetDouble(1)`, `_Z8UpdateFSv`, `AddChannel`, `_Z12UpdateTimersv`):** Proved that entering Level 1 calls `_Z9SetDoublei(1)` (`0x00bac0`), switching internal rendering to `480×320` (`4×` pixel count) followed by software 2×2 box-filter downsampling in `_Z8UpdateFSv` (`0x00b838`, `1.38M` instructions/frame). When the full multi-channel `.XM` arrangement kicks in at modeled second 38, `libmikmod`'s 64-bit `AddChannel` mixer (`0x05d74c`, `FRACBITS = 28`) surges from `6.9M` to **`25.2M` instructions/s (`63.49%` of the `40M` budget at `Ratio 2`)**, starving the main thread into `_Z12UpdateTimersv`'s `frameTicks > 32` catch-up spiral (`2.4 FPS`).
  * **Direct ARM FPA11 (`cp1`/`cp2`) Hardware Coprocessor Execution (`src/cpu/arm920t.cpp`):** Implemented `LDFS`/`STFS`/`LDFD`/`STFD`, `LFM`/`SFM`, `FLT`, `FIX`, `WFS`/`RFS`/`WFC`/`RFC`, `CMF`/`CMNF`/`CMFE`/`CMNFE`, and all single/double `CPDO` arithmetic/transcendental operations directly in `ARM920T` (`--fpa11 on|off`, default `on`), eliminating **100% of `NWFPE` trap overhead (`65.33% -> 0.00%`)** and cutting the Arcade Lab loading freeze from **`14 s` to `3 s`** (`23 s -> 10 s` to Title Screen at `Ratio 3`).
  * **Host Throughput (`25.48 -> 101.61 MIPS`), Windows Power-Throttling Fix & `Ratio 3` Verification:** Added `ADC`/`SBC`/`RSC` to `executeSimpleDataProcessing`, ordered `bus.cpp` and `arm920t.cpp` before `main.cpp` in unity/PGO builds (`74.09 MIPS` standard `make`, **`101.61 MIPS`** PGO `bin/oceanblast.exe`), disabled Windows 11 `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` (`EcoQoS`) for embedded `--parent-hwnd` sessions, and throttled `DirectInput8`/`XInput` polling in `Display::processEvents()` to `125 Hz`. Verified `100.0%` real-time speed (`60.0 MIPS`), zero dropped audio samples, and `15–22` presented FPS in late gameplay across a 60-second live `--gui --sound` session ([`2026-10-11_chefs_fpa11_ratio3_gui_performance.csv`](docs/validation/2026-10-11_chefs_fpa11_ratio3_gui_performance.csv)).

---

## 2026-10-10 — Executable Attribution, Framebuffer Modes, CPU Throughput & Controller Frontend

* **Native Windows Launcher, Embedded Gameplay & Controller Input ([docs/43](docs/43_windows_launcher_and_controller_input.md)):**
  * Redesigned the Windows graphical frontend (`bin/oceanblast.exe`) around a `1024×768` resizable window, dark ocean-navy start screen with embedded `assets/oceanblast-logo.png` and `assets/oceanblast-symbol.png` taskbar icon, `Datei` | `Einstellungen` menu bar, `Strg+O` and Drag-and-Drop ROM loading, and single-window embedded gameplay via `--parent-hwnd`).
  * Added full **XInput**, **DirectInput8**, and **Google Stadia Controller** support (`src/display/input_mapping.h`, `src/display/windows_input.h`), mapping D-Pad (POV hat + left analog stick), A/B/C, L/R shoulders, Start/Stop, Select/Play-Pause, and L2/R2 media rewind/fast-forward.
  * Added the `850×595` **`OceanBlast — Einstellungen`** dialog (`Ordner & System`, `Steuerung`, `Sound`, `Video`, `Timing & Diagnose`, `Kurztasten & Hilfe`) featuring a live 25-ms GDI+ controller schematic, one-click controller presets (`Stadia / Xbox`, `PlayStation`, `digiBLAST / USB`), interactive click-to-learn button/key assignment, and INI persistence (`%APPDATA%\OceanBlast\windows.ini`).
* **Handheld 12-bpp LCD Mode via `GPH8 = 1` Across Cartridges ([docs/42](docs/42_wade_gph8_lcd_mode.md)):**
  * Modeled S3C2410 GPIO pin **`GPH8`** (`0x56000074` bit 8) as a pulled-high input when `GPHCON` bits `[17:16] == 00`, so `s3c2410fb_probe` boots in native handheld 12-bpp `'LCD'` mode (`use_tvout = 0`, `LCDCON1 = 0x14c9`, `LCDSADDR3 = 0xb4` = `360` bytes/row) instead of PAL 16-bpp TV-Out mode.
  * In *Wade Hixton's Counter Punch*, eliminating the 16-bpp shadow surface removes `BlitNtoN` completely (`51.33% -> 0.00%`, replaced by `FB_LCD444Update` at `10.08%`) and cuts active-fight CPU work by **50.0%** (`4.50M -> 2.25M` step calls/frame; `144M` step calls per 64-frame counter unit, fitting within a 180 MHz ARM920T cycle budget).
  * Audited all 11 games and 3 combined cartridges: *Crazy Jack*, *DigiQUAD*, *Superstar Chefs*, *Wade Hixton*, and all 3 combined menus switch to native `57,600`-byte 12-bpp LCD444 mode, resolving *Crazy Jack*'s register/pixel-format disagreement ([docs/25](docs/25_automatic_display_selection.md)) and boot-splash conversion artifacts natively.
* **Wade Executable & `s3c2410fb` Driver Disassembly ([docs/41](docs/41_wade_binary_and_s3c2410fb_analysis.md)):**
  * Extracted the unstripped `Wade` ELF binary from the cartridge's SquashFS image and performed byte-verified function attribution across ratios 4 and 16.
  * Confirmed the round counter decrements strictly every 64 frames (`GLOBAL_nFrameCounter & 0x3f` in `_Z10_PlayRoundv`), with a 16-ms frame limiter (`vblTime = 16` at `0x005b9840` in `_Z11vblank_waitv`) and 1-µs sub-jiffy `gettimeofday` interpolation via `s3c2410_gettimeoffset` (`TCNTO4` at `0x51000040`).
  * Identified internal `BGR444` (`0x000f, 0x00f0, 0x0f00`) vs. `RGB444` (`0x0f00, 0x00f0, 0x000f`) surface mask mismatches in `Wade.elf` and traced the kernel's 16-bpp RGB565 reply to active-low TV-Out sense pin **`GPH8`** (`GPHDAT` `0x56000074` bit 8) in `s3c2410fb_probe`.
* **SDL Format & Work-Scaling Diagnostics ([docs/40](docs/40_wade_sdl_formats_and_work_scaling.md)):**
  * Byte-verified `libSDL-1.2.so.0.7.1` attribution showed `BlitNtoN` (`51.33%`) and `BlitNtoNKey` (`31.27%`) dominating fight execution, and captured `SDL_SetVideoMode(240, 160, 12, 0)` receiving `bits_per_pixel=16` from `FBIOPUT_VSCREENINFO`.
* **Cartridge Hardware Documentation ([docs/39](docs/39_photographed_cartridge_hardware.md)):**
  * Documented photographed physical cartridge PCB hardware and large-page (`2,048 + 64` byte) vs. small-page (`512 + 16` byte) NAND flash organization.
* **Guest Time & LCD Wait Diagnostics ([docs/38](docs/38_guest_time_and_lcd_wait_diagnostics.md)):**
  * Verified exact 200-Hz Timer 4 jiffies progression (`0xc017f1cc`) during Wade's active fight with zero lost timer ticks and zero guest LCD MMIO/ioctl waits.
* **CPU Fetch & Interpreter Execution ([docs/37](docs/37_cpu_fetch_and_interpreter_execution.md)) & Wade Performance ([docs/36](docs/36_wade_hixon_performance.md)):**
  * Improved controlled instruction throughput by 17–45% relative to the preceding optimized build while preserving identical guest CPU/SDRAM states.

---

## 2026-10-09 — Display Coherence, Console Skin, RealVideo & Register Audits

* **Direct LCD Frame Recording ([docs/35](docs/35_lcd_frame_recording.md)) & Automatic Cartridge Settings ([docs/34](docs/34_automatic_cartridge_settings.md)):**
  * Added deterministic LCD frame capture and per-cartridge automatic CPU ratio selection for known dumps (*Pitfall*, *Superstar Chefs*, *Wade Hixton*).
* **Console Skin & Fullscreen Controls ([docs/33](docs/33_console_skin_and_fullscreen.md)):**
  * Added the interactive silver digiBLAST console skin (`--window-mode skin`), borderless fullscreen (`F11` / `Alt+Enter`), and reverse-engineered media player keypad controls (`KEY_P`, `KEY_S`, `KEY_B`, `KEY_N`).
* **Coherent Video Frames & VSync ([docs/32](docs/32_coherent_video_and_vsync.md)) & Programmed Scanout Height ([docs/31](docs/31_programmed_scanout_height.md)):**
  * Prevented presenting partially written video frames, added DXGI vertical synchronization, and honored programmed 240-row LCD scanout heights.
* **RealVideo Frame Delivery ([docs/30](docs/30_realvideo_frame_delivery.md)) & Gameplay Execution Cost ([docs/29](docs/29_gameplay_work_and_execution_cost.md)):**
  * Demonstrated decoded colored video frames on *Sonic X* and Italian/Spanish *Winx* at higher diagnostic CPU allowances and profiled software audio/blitter workloads.
* **Cartridge Register Audit ([docs/28](docs/28_cartridge_register_audit.md)), Windows Host Pacing ([docs/27](docs/27_windows_host_pacing.md)) & Execution Batches ([docs/26](docs/26_execution_batches_and_mmio_diagnostics.md)):**
  * Audited all 11 game cartridges and 9 video cartridges across multi-billion-instruction runs without kernel panics or guest segmentation faults; introduced bounded execution batches and high-resolution waitable-timer host pacing.

---

## 2026-10-08 — Clocks, DMA Audio Lifetime, I2C EEPROM & Cartridge Matrix

* **Automatic Display Selection ([docs/25](docs/25_automatic_display_selection.md)) & Superstar Chefs Load Analysis ([docs/24](docs/24_automatic_timing_and_chefs_load.md)):**
  * Added automatic framebuffer layout recognition for *Crazy Jack* and analyzed *Superstar Chefs* CPU load vs. audio queue starvation.
* **Register-Derived Clocks & IIS Pause Control ([docs/23](docs/23_register_clocks_and_iis_pause.md)), CPU Budget ([docs/22](docs/22_cpu_budget_and_runtime_timing.md)) & Hardware Reference ([docs/21](docs/21_pitfall_original_hardware_reference.md)):**
  * Derived FCLK/HCLK/PCLK and Timer 4 / IIS audio rates directly from guest PLL and divider registers (`MPLLCON`, `CLKDIVN`, `IISPSR`).
* **Consumed-Sample DMA Capture ([docs/20](docs/20_pcm_capture_and_dma_sample_lifetime.md)) & DMA Streaming ([docs/19](docs/19_dma_streaming_and_interrupt_delivery.md)):**
  * Fixed DMA Channel 2 source RAM reuse artifacts by capturing PCM samples as they are consumed by the simulated IIS FIFO.
* **Manual Cartridge Validation ([docs/18](docs/18_manual_cartridge_validation.md)) & Timer 4 Validation ([docs/17](docs/17_timer_and_runtime_validation.md)):**
  * Documented controlled GUI/audio sessions across *DigiQUAD*, *Wade Hixton*, *Superstar Chefs*, *Spider-Man*, and *Rayman 3*; restored first episode imagery on video-only Italian/Spanish *Winx*.
* **I2C EEPROM & Player Startup ([docs/16](docs/16_i2c_eeprom_and_player_startup.md)) & UART0 Interrupts ([docs/15](docs/15_uart_video_startup.md)):**
  * Implemented 2-KB I2C EEPROM (`0x54000000`) and level-triggered UART0 TX interrupts, resolving startup blockers in *Crazy Jack*, *Superstar Chefs*, and combined game+video cartridges.
* **ROM Integrity ([docs/14](docs/14_rom_integrity.md)), Video Probe ([docs/13](docs/13_video_games_probe.md)) & Game Compatibility Baseline ([docs/12](docs/12_game_cartridge_compatibility.md)):**
  * Cross-checked 25 local cartridge dumps against MAME reference hashes and fixed ARM/Thumb instruction decoding defects (`LDR`/`STR` register offsets, `PUSH`/`POP`, `TST`/`TEQ` shifter carry, word rotation).

---

## Early Bring-Up — Bootloader, Linux 2.6.11 Kernel, Display & Audio Foundations

* **Audio Timing & Sample Rate ([docs/11](docs/11_audio_timing_followup.md), [docs/09](docs/09_audio_sample_rate_fix.md)):**
  * Implemented streaming linear audio resampling, waveOut buffer aggregation, and DMA position tracking (`DCSRC2`, `DSTAT2`).
* **Dual Color Depth Display ([docs/10](docs/10_color_depth_and_rgb565_support.md)) & Performance ([docs/08](docs/08_performance_optimizations.md), [docs/07](docs/07_runtime_diagnostics.md)):**
  * Added 16-bit RGB565 and 12-bit packed RGB444 (`LCD444`) scanout decoding and fast host page-table translation caching.
* **Windows Launcher ([docs/06](docs/06_windows_launcher.md)) & Black-Screen Investigation ([docs/05](docs/05_black_screen_investigation.md)):**
  * Built the native Win32 launcher UI and implemented ARM920T MMU Access Permission (`AP`) fault generation (`FSR = 0xF`) required for Linux Copy-On-Write (`do_wp_page()`).
* **CPU Review ([docs/04](docs/04_cpu_review.md)), Boot Progress ([docs/03](docs/03_boot_progress.md)), Validation ([docs/02](docs/02_sources_and_validation.md)), Memory Map ([docs/01](docs/01_memory_map.md)) & Hardware Specs ([docs/00](docs/00_hardware_specs.md)):**
  * Initial S3C2410 Steppingstone 4-KB boot SRAM, NAND Flash controller with Toshiba Read ID (`0x98, 0x73/0x75/0x76`) and Hamming ECC synthesis for U-Boot `nidc`, ADC battery check (`checkbattery`), U-Boot 1.1.2 execution, Linux 2.6.11 kernel decompression, SquashFS 2.2 rootfs mount, and `greykbd` GPIO keypad mapping.
