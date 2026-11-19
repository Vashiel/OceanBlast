# OceanBlast Technical Documentation

This directory contains the architectural reference documentation and empirical validation archive for **OceanBlast** (Nikko digiBLAST / Samsung S3C2410A ARM920T emulator).

For user instructions and cartridge compatibility, see the main [Project README](../README.md). For active milestones, see [ROADMAP.md](../ROADMAP.md).

---

## 1. Current Architecture & Subsystem Reference

| Area | Current Implementation | Primary Reference Reports |
| :--- | :--- | :--- |
| **Hardware & Memory Map** | Samsung OCEAN-L-20 (S3C2410A, ARM920T), 4 KB Steppingstone SRAM, 32 MB SDRAM, NAND Flash (512+16 OOB & ECC), 2-KB I2C EEPROM | [00 Hardware Specs](00_hardware_specs.md), [01 Memory Map](01_memory_map.md), [39 Cartridge Hardware](39_photographed_cartridge_hardware.md) |
| **x86-64 JIT Execution Engine** | Shared ARM/Thumb IR, single-pass AsmJit emitter, Windows x64 & Linux System-V entry bridge (`R12`/`R13`), 4,096-bucket intrusive code cache, 2-way guarded window links, inline TLB reads, per-4 KB TLB mapping generations | [92 JIT Architecture](92_jit_architecture_decision.md), [107 Shared Native Windows](107_shared_native_windows.md), [115 Shared Entry ABI & Dispatch](115_shared_native_entry_and_dispatch.md), [116 Per-Page TLB & Flat MMIO](116_page_tlb_generation_and_flat_mmio.md), [117 Linux x64 JIT & ARM64 SDL2](117_linux_x64_jit_and_arm64_sdl2.md) |
| **ARM920T Interpreter, FPA11 & MMU** | Reference/fallback interpreter (also ARM64 Linux primary engine), direct FPA11 (`cp1`/`cp2`) hardware float execution, compact 8-byte 2,048-entry TLB, $O(1)$ MVA invalidation, flat `O(1)` S3C2410 MMIO register table | [44 FPA11 & Chefs Analysis](44_superstar_chefs_analysis_and_fix.md), [45 Presets, GPIO & TLB](45_cartridge_presets_gpio_keys_and_tlb_invalidation.md), [82 Compact TLB](82_compact_tlb_entries.md), [116 Flat MMIO](116_page_tlb_generation_and_flat_mmio.md), [117 Linux/ARM64 Build](117_linux_x64_jit_and_arm64_sdl2.md) |
| **Timing, Clocks & Audio** | Shared 20-MHz peripheral time unit, PLL-derived FCLK/HCLK/PCLK, 200-Hz Timer 4 (`TCNTO4` interpolation), consumed-sample DMA2 IIS capture, clocked-native opt-in (`--jit-clocked`) | [20 PCM Capture & DMA2](20_pcm_capture_and_dma_sample_lifetime.md), [23 Register Clocks & IIS](23_register_clocks_and_iis_pause.md), [110 Common Timing Model](110_common_timing_model_and_calibration.md), [113 Clocked Native Timeline](113_clocked_native_timeline.md) |
| **Display & Video (`s3c2410fb`)** | Handheld 12-bpp LCD444 (`GPH8 = 1` strap) & 16-bpp RGB565 decoding, coherent frame latch, DXGI VSync / GDI presentation on Windows and SDL2 renderer on Linux | [10 RGB565 Support](10_color_depth_and_rgb565_support.md), [32 Coherent Video & VSync](32_coherent_video_and_vsync.md), [42 Handheld 12-bpp LCD Mode](42_wade_gph8_lcd_mode.md), [117 SDL2 Backend](117_linux_x64_jit_and_arm64_sdl2.md) |
| **Frontend, Input & MP3** | 7-language Win32 GUI launcher, XInput/DirectInput8/Stadia remapping, console skin, opt-in OHCI USB 256-MB FAT16 MP3 storage manager, and cross-platform SDL2 keyboard/GameController backend | [33 Console Skin & Fullscreen](33_console_skin_and_fullscreen.md), [43 Windows Launcher & Input](43_windows_launcher_and_controller_input.md), [49 Media & Cartridge Language](49_media_input_and_cartridge_language.md), [51 MP3 Storage & Player](51_mp3_storage_and_player.md), [117 SDL2 Input](117_linux_x64_jit_and_arm64_sdl2.md) |

---

## 2. Historical Research & Validation Archive (`00`–`117`)

All step-by-step reverse-engineering notes, profiling sessions, and candidate experiments (including rejected hypotheses) are preserved below for reproducibility. Machine-readable benchmark manifests and CSV traces reside in [`validation/`](validation/).

<details>
<summary><strong>Phase 4 — Native x86-64 JIT Engine, Linux/ARM64 Port & Common Timing Model (Reports 92–117)</strong></summary>

* [117 — Linux x86-64 System-V JIT, ARM64 interpreter build and SDL2 backends](117_linux_x64_jit_and_arm64_sdl2.md)

* [116 — Per-page TLB mapping generation, compiled-block retention and flat MMIO table](116_page_tlb_generation_and_flat_mmio.md)
* [115 — Shared native entry ABI and intrusive dispatch streamlining](115_shared_native_entry_and_dispatch.md)
* [114 — Chefs battery memory-path review](114_chefs_battery_memory_review.md)
* [113 — Clocked native execution timeline](113_clocked_native_timeline.md)
* [112 — Chefs battery countercheck](112_chefs_battery_countercheck.md)
* [111 — Chefs installed/JIT resource comparison](111_chefs_resource_comparison.md)
* [110 — Common timing model and calibration plan](110_common_timing_model_and_calibration.md)
* [109 — Manual JIT package and two-game counter-check](109_manual_jit_package_and_cross_game_checks.md)
* [108 — Shared native entry ABI fixture](108_shared_native_entry_abi.md)
* [107 — Shared native execution windows](107_shared_native_windows.md)
* [106 — Execution-window delivery gate](106_execution_window_delivery_gate.md)
* [105 — ARM multiple transfers and PGO check](105_arm_multiple_transfers_and_pgo.md)
* [104 — Ordinary ARM register returns](104_arm_register_returns.md)
* [103 — Native coverage and sampled compilation](103_native_coverage_and_sampled_compilation.md)
* [102 — Dispatch and region candidates](102_dispatch_and_region_candidates.md)
* [101 — Conditional fallthrough experiment (rejected)](101_conditional_fallthrough_rejected.md)
* [100 — Native regions and precise exits](100_native_regions_and_precise_exits.md)
* [99 — Cached dispatch and mapping context](99_cached_dispatch_and_mapping_context.md)
* [98 — Mixed RAM blocks](98_mixed_ram_blocks.md)
* [97 — Checked memory and player integration](97_checked_memory_and_player_integration.md)
* [96 — Initial x86-64 native execution](96_initial_x64_execution.md)
* [95 — Shared translation frontend](95_shared_translation_frontend.md)
* [94 — Shared state and execution boundary](94_shared_state_execution_boundary.md)
* [93 — Planning retrospective](93_personal_planning_note.md)
* [92 — Committed JIT architecture decision](92_jit_architecture_decision.md)

</details>

<details>
<summary><strong>Phase 3 — Core Extraction, Interpreter Fast Paths & Audio Reference Analysis (Reports 53–91)</strong></summary>

* [91 — Chefs EEPROM difficulty and health fix](91_chefs_eeprom_health.md)
* [90 — Wade single-buffer music startup](90_wade_music_single_buffer.md)
* [89 — Wade music startup delivery](89_wade_music_startup_delivery.md)
* [88 — digiBLAST XM soundtrack reference](88_digiblast_soundtrack_reference.md)
* [87 — Local GBA Wade comparison](87_local_gba_wade_comparison.md)
* [86 — GBA title reference analysis](86_gba_title_reference.md)
* [85 — Wade PCM comparison and audio recovery check](85_wade_pcm_and_audio_recovery.md)
* [84 — Wade title audio starvation diagnosis](84_wade_title_audio_starvation.md)
* [83 — Consolidated PGO executable acceptance](83_consolidated_pgo_acceptance.md)
* [82 — Compact 8-byte TLB entries](82_compact_tlb_entries.md)
* [81 — Current Wade profile and Chefs check](81_current_wade_profile_and_chefs_check.md)
* [80 — Controlled total before/after comparison](80_controlled_total_comparison.md)
* [79 — Fixed-ratio legacy batch experiment (rejected)](79_legacy_fixed_ratio_experiment.md)
* [78 — Zero-tick peripheral bypass](78_zerotick_peripheral_bypass.md)
* [77 — Post-shortcut active-scene host profile](77_post_shortcut_host_profile.md)
* [76 — Tick/step branch-hint experiment (rejected)](76_tick_step_checks_experiment.md)
* [75 — Inlined word/halfword transfer experiment (rejected)](75_step_loads_inline_experiment.md)
* [74 — step-to-stepARM dispatch experiment (rejected)](74_steparm_noflags_dispatch_experiment.md)
* [73 — Data MMU and bus read/write fast-path streamlining](73_mmu_bus_fastpath.md)
* [72 — Combined instruction-fetch check](72_instruction_fetch_check.md)
* [71 — SUBS/CMP simple-ALU experiment (rejected)](71_subs_cmp_experiment.md)
* [70 — Pre-indexed immediate halfword transfer shortcut](70_halfword_transfer_shortcut.md)
* [69 — Pre-indexed immediate word transfer shortcut](69_word_transfer_shortcut.md)
* [68 — Register-ALU no-flags shortcut](68_register_noflags_shortcut.md)
* [67 — Active-scene guest instruction mix](67_active_scene_instruction_mix.md)
* [66 — Flag-setting immediate extension (rejected)](66_immediate_flags_experiment.md)
* [65 — Active-scene host profile](65_active_scene_host_profile.md)
* [64 — LDM RAM-span experiment (rejected)](64_ldm_ram_span_experiment.md)
* [63 — MMU table-walk outlining experiment (rejected)](63_mmu_outline_experiment.md)
* [62 — Active-scene acceptance fixture](62_active_scene_acceptance.md)
* [61 — ARM dispatch measurement and immediate path](61_arm_dispatch_measurement.md)
* [60 — Legacy phase batches](60_legacy_phase_batches.md)
* [59 — Machine core and decoder experiment](59_machine_core_and_decoder_experiment.md)
* [58 — Execution boundary and short checks](58_execution_boundary_and_short_checks.md)
* [57 — TLB permission-tag candidate checks](57_tlb_permission_tag_candidates.md)
* [56 — Host fast-path candidate validation](56_host_fast_path_candidates.md)
* [55 — Frontend settings persistence](55_frontend_settings_persistence.md)
* [54 — Host CPU sample profile](54_host_cpu_samples.md)
* [53 — Host execution cost measurement](53_host_execution_costs.md)

</details>

<details>
<summary><strong>Phase 2 — Full Game Library Compatibility, FPA11, Launcher & MP3 Storage (Reports 31–52)</strong></summary>

* [52 — Windows 7–11 and x86 compatibility builds](52_windows_7_and_x86_builds.md)
* [52 — Wade ratio 6 and host power](52_wade_ratio6_and_host_power.md)
* [51 — MP3 storage and original player integration](51_mp3_storage_and_player.md)
* [50 — Reviewed-build runtime acceptance](50_runtime_acceptance.md)
* [49 — Media input and cartridge language](49_media_input_and_cartridge_language.md)
* [48 — MP3 cartridge firmware probe](48_mp3_cartridge_firmware_probe.md)
* [47 — Controller settings repaint](47_controller_settings_repaint.md)
* [46 — Runtime build profiles and Rayman 3 ratio](46_runtime_build_profiles_and_rayman_ratio.md)
* [45 — Cartridge presets, GPIO keys and TLB invalidation](45_cartridge_presets_gpio_keys_and_tlb_invalidation.md)
* [44 — Superstar Chefs analysis and FPA11 hardware execution](44_superstar_chefs_analysis_and_fix.md)
* [43 — Windows launcher and controller input](43_windows_launcher_and_controller_input.md)
* [42 — Handheld 12-bpp LCD mode via GPH8](42_wade_gph8_lcd_mode.md)
* [41 — Wade binary and s3c2410fb analysis](41_wade_binary_and_s3c2410fb_analysis.md)
* [40 — Wade SDL formats and work scaling](40_wade_sdl_formats_and_work_scaling.md)
* [39 — Photographed cartridge hardware](39_photographed_cartridge_hardware.md)
* [38 — Guest time and LCD wait diagnostics](38_guest_time_and_lcd_wait_diagnostics.md)
* [37 — CPU fetch and interpreter execution](37_cpu_fetch_and_interpreter_execution.md)
* [36 — Wade Hixton performance](36_wade_hixon_performance.md)
* [35 — LCD frame recording](35_lcd_frame_recording.md)
* [34 — Automatic cartridge settings](34_automatic_cartridge_settings.md)
* [33 — Console skin and fullscreen](33_console_skin_and_fullscreen.md)
* [32 — Coherent video and VSync](32_coherent_video_and_vsync.md)
* [31 — Programmed scanout height](31_programmed_scanout_height.md)

</details>

<details>
<summary><strong>Phase 1 — Initial Hardware Bring-Up, U-Boot, Linux 2.6.11 & Peripherals (Reports 00–30)</strong></summary>

* [30 — RealVideo frame delivery](30_realvideo_frame_delivery.md)
* [29 — Gameplay work and execution cost](29_gameplay_work_and_execution_cost.md)
* [28 — Cartridge register audit](28_cartridge_register_audit.md)
* [27 — Windows host pacing](27_windows_host_pacing.md)
* [26 — Execution batches and MMIO diagnostics](26_execution_batches_and_mmio_diagnostics.md)
* [25 — Automatic display selection](25_automatic_display_selection.md)
* [24 — Automatic timing and Chefs load](24_automatic_timing_and_chefs_load.md)
* [23 — Register clocks and IIS pause](23_register_clocks_and_iis_pause.md)
* [22 — CPU budget and runtime timing](22_cpu_budget_and_runtime_timing.md)
* [21 — Pitfall original hardware reference](21_pitfall_original_hardware_reference.md)
* [20 — PCM capture and DMA sample lifetime](20_pcm_capture_and_dma_sample_lifetime.md)
* [19 — DMA streaming and interrupt delivery](19_dma_streaming_and_interrupt_delivery.md)
* [18 — Manual cartridge validation](18_manual_cartridge_validation.md)
* [17 — Timer and runtime validation](17_timer_and_runtime_validation.md)
* [16 — I2C EEPROM and player startup](16_i2c_eeprom_and_player_startup.md)
* [15 — UART and video startup](15_uart_video_startup.md)
* [14 — ROM integrity](14_rom_integrity.md)
* [13 — Video and games probe](13_video_games_probe.md)
* [12 — Game cartridge compatibility](12_game_cartridge_compatibility.md)
* [11 — Audio timing follow-up](11_audio_timing_followup.md)
* [10 — Color depth and RGB565 support](10_color_depth_and_rgb565_support.md)
* [09 — Audio sample rate fix](09_audio_sample_rate_fix.md)
* [08 — Performance optimizations](08_performance_optimizations.md)
* [07 — Runtime diagnostics](07_runtime_diagnostics.md)
* [06 — Windows launcher](06_windows_launcher.md)
* [05 — Black screen investigation](05_black_screen_investigation.md)
* [04 — CPU review](04_cpu_review.md)
* [03 — Boot progress](03_boot_progress.md)
* [02 — Sources and validation](02_sources_and_validation.md)
* [01 — Memory map](01_memory_map.md)
* [00 — Hardware specifications](00_hardware_specs.md)

</details>
