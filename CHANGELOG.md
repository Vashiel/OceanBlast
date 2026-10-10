# Changelog & Technical Investigation History

This changelog records the chronological development, hardware reverse-engineering milestones, and empirical validation reports (`docs/00`–`docs/41`) for **OceanBlast**.

---

## 2026-10-10 — Executable Attribution, Framebuffer Modes & CPU Throughput

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
