# Development Roadmap

Updated: 2026-11-16. For the full technical documentation index and historical research reports, see [docs/README.md](docs/README.md) and [CHANGELOG.md](CHANGELOG.md).

---

## 1. Execution Architecture & Milestones

The [committed JIT architecture](docs/92_jit_architecture_decision.md) defines the active execution design: a shared ARM/Thumb translation frontend and typed IR, a Windows x86-64 native backend first, and an Android ARM64 backend next, with the ARM920T interpreter retained as an exact reference and fallback engine.

| Priority | Milestone | Status | Completion Criterion & Primary Reference |
| :---: | :--- | :---: | :--- |
| **J0** | Shared engine/state boundary and precise exits | **Complete** | Interpreter, state handoff, interrupt and timing-phase checks pass ([docs/94](docs/94_shared_state_execution_boundary.md)). |
| **J1** | ARM/Thumb decoder, typed IR and bounded block metadata | **Complete** | 4,680 semantic comparisons agree with reference; unsupported opcodes exit explicitly ([docs/95](docs/95_shared_translation_frontend.md)). |
| **J2** | Windows x86-64 code generation, shared entry ABI & intrusive cache | **Complete** | Single-pass `x86::Assembler`, shared `R12`/`R13` entry bridge, 4,096-bucket intrusive cache and 2-way window linking ([docs/107](docs/107_shared_native_windows.md), [docs/115](docs/115_shared_native_entry_and_dispatch.md)). |
| **J3** | Memory, faults, per-page TLB generations & cache invalidation | **Complete** | Inline TLB reads, per-4 KB `pageMappingGeneration`, alias-write/DMA epoch invalidation and flat `O(1)` MMIO table ([docs/97](docs/97_checked_memory_and_player_integration.md), [docs/116](docs/116_page_tlb_generation_and_flat_mmio.md)). |
| **J4** | Whole-machine JIT integration and instruction coverage | **Delivered (In Progress)** | ARM shifts, halfword/signed loads, register returns, `LDM`/`STM`, suffix loops and clocked-native timeline pass all 13 CTest suites and preserve canonical Chefs/Wade states ([docs/103](docs/103_native_coverage_and_sampled_compilation.md)–[docs/116](docs/116_page_tlb_generation_and_flat_mmio.md)). |
| **T0–T4** | Common timing model and hardware calibration | **T0 & Initial T3 Complete** | Shared 20-MHz peripheral time unit, explicit model reporting and opt-in clocked-native execution (`--jit-clocked`) delivered ([docs/110](docs/110_common_timing_model_and_calibration.md), [docs/113](docs/113_clocked_native_timeline.md)); hardware video calibration remains open. |
| **J5** | Production Windows acceptance & weak-hardware verification | **In Progress** | AC and live gameplay sessions reach ~90% native coverage; battery-power startup audio reserve and low-end hardware measurements remain open gates ([docs/112](docs/112_chefs_battery_countercheck.md)). |
| **J6** | Android ARM64 backend and device integration | **Planned** | Shared IR lowers to ARM64 with real-device pacing/audio/reserve verification. |

---

## 2. Current Delivery Summary

| Area | Current Status | Summary & Validation Reference |
| :--- | :--- | :--- |
| **Linux x86-64 JIT, ARM64 Linux & SDL2 Frontend** | **Complete** | [docs/117](docs/117_linux_x64_jit_and_arm64_sdl2.md): ported `X64ExecutionEngine` and `tests/x64_entry_abi.cpp` to the System-V AMD64 ABI with `/proc/self/maps` `r-x` span verification and AsmJit dual mapping, added portable SDL2 display/audio/controller backends (`src/display/display_sdl.cpp`, `src/audio/audio_sdl.cpp`), and added `CMakeLists.txt` + `cmake/aarch64-linux-gnu.cmake` for ARM64 Linux (`aarch64`). Linux x86-64 passes 13/13 CTest suites (65.83 s) and matches Windows canonical Wade 2.4B (21.748 s) and Chefs 600M (5.420 s) hashes bit-for-bit; ARM64 Linux passes 19/19 portable CTests (15.39 s) and Chefs 10M state parity under `qemu-aarch64-static`. Interactive X11/Wayland display and real ARM64 silicon testing remain external host checks. |
| **Per-page TLB generation, compiled-block retention & flat MMIO** | **Complete** | [docs/116](docs/116_page_tlb_generation_and_flat_mmio.md): per-4 KB `pageMappingGeneration` on single-entry TLB flushes, compiled-block retention across sampling collisions, slab-cached `VirtualQuery` verification and flat `O(1)` MMIO array pass all 13 CTest suites (20.00 s). Mapping revalidations drop by **99.46%** on Wade (6.90M → 37.3k) and **99.20%** on Chefs (3.74M → 30.0k); Wade 2.4B native coverage rises from **66.01% to 89.87%** (`sampled_fallback_steps` **-74.86%**) and process CPU time drops by a further **17.83%** (**-25.39%** vs pre-115 `HEAD`). |
| **Shared native entry ABI & intrusive dispatch streamlining** | **Complete** | [docs/115](docs/115_shared_native_entry_and_dispatch.md): single shared Windows x64 entry/exit bridge (`R12`/`R13`, single-pass `x86::Assembler`), 4,096-bucket intrusive `cacheByPc` table, splice-free LRU tracking and two-way successor slots pass all 13 suites. Peak code-cache footprint drops by 9.8–11.7% and slow window lookups drop by 67–77% with bit-identical CPU/SDRAM hashes. |
| **Common timing model & clocked-native timeline** | **T0 & Initial T3 Complete** | [docs/110](docs/110_common_timing_model_and_calibration.md), [docs/113](docs/113_clocked_native_timeline.md): unified CPU/Timer/DMA time unit and opt-in cycle-accounted JIT (`--engine jit --timing auto --jit-clocked`) pass all 13 suites and short state comparisons. Original-hardware tempo calibration remains open. |
| **Per-cartridge NVRAM & Superstar Chefs health fix** | **Complete** | [docs/91](docs/91_chefs_eeprom_health.md): content-keyed `sessions/nvram/` EEPROM files preserve legacy saves and repair invalid persisted difficulty 80 in Superstar Chefs. |
| **Battery-power startup reserve & weak-hardware checks** | **Open** | [docs/112](docs/112_chefs_battery_countercheck.md), [docs/114](docs/114_chefs_battery_memory_review.md): AC gameplay runs cleanly, while throttled battery startup before the first title input and measurements on older low-end hardware remain open validation items. |
| **Windows x86 (32-bit) speed** | **Open** | The interpreter-only build reaches about 96 MIPS on *Wade*; ratio 6 needs about 120. Candidates: a 32-bit JIT backend or further interpreter work. 64-bit Windows users should use the x64 package. |

---

## 3. Engineering & Validation Rules

1. **Short, targeted checks first:** CPU, MMU, JIT and timing changes must pass all 13 ROM-free CTest suites (`ctest --test-dir build/jit --output-on-failure`) before any cartridge benchmark.
2. **State equivalence:** Any execution or memory optimization must preserve canonical `state.cpu.bin` and 32 MB `state.ram.bin` SHA-256 hashes on deterministic replay fixtures.
3. **Controlled comparisons:** Benchmark pairs must use matching compiler flags, power state and processor affinity, and record exact binary/state hashes in [`docs/validation/`](docs/validation/).

---

<details>
<summary><strong>Historical Pre-JIT Interpreter & Foundation Archive (2026-10-12 to 2026-10-18, Reports 58–85)</strong></summary>

The entries below preserve the earlier interpreter fast-path experiments and core-extraction milestones that preceded the x86-64 JIT backend:

* **State & Scheduler Extraction ([docs/58](docs/58_execution_boundary_and_short_checks.md), [docs/59](docs/59_machine_core_and_decoder_experiment.md), [docs/60](docs/60_legacy_phase_batches.md)):** Extracted `ARM920TState`, standalone `Arm920TMmu`, `ExecutionScheduler`, shared `Machine` owner, and legacy phase batching.
* **Retained Interpreter Fast Paths ([docs/61](docs/61_arm_dispatch_measurement.md)–[docs/83](docs/83_consolidated_pgo_acceptance.md)):**
  * Immediate ALU no-flags shortcut ([docs/61](docs/61_arm_dispatch_measurement.md), [docs/62](docs/62_active_scene_acceptance.md))
  * Register ALU no-flags shortcut ([docs/67](docs/67_active_scene_instruction_mix.md), [docs/68](docs/68_register_noflags_shortcut.md))
  * Pre-indexed immediate word and halfword transfer shortcuts ([docs/69](docs/69_word_transfer_shortcut.md), [docs/70](docs/70_halfword_transfer_shortcut.md))
  * Combined XOR-and-mask instruction fetch check ([docs/72](docs/72_instruction_fetch_check.md))
  * Streamlined data MMU permission check and physical SDRAM bounds ([docs/73](docs/73_mmu_bus_fastpath.md))
  * Zero-tick peripheral bypass in `Bus::tick(0)` ([docs/77](docs/77_post_shortcut_host_profile.md), [docs/78](docs/78_zerotick_peripheral_bypass.md))
  * Controlled total interpreter comparison (+32.13% fight throughput, -22.91% CPU time in [docs/80](docs/80_controlled_total_comparison.md))
  * Compact 8-byte 2,048-entry TLB layout ([docs/81](docs/81_current_wade_profile_and_chefs_check.md), [docs/82](docs/82_compact_tlb_entries.md)) and consolidated PGO build ([docs/83](docs/83_consolidated_pgo_acceptance.md))
* **Rejected Interpreter Candidates (Not Retained):** MMU table-walk outlining ([docs/63](docs/63_mmu_outline_experiment.md)), LDM RAM-span translation ([docs/64](docs/64_ldm_ram_span_experiment.md)), flag-setting immediate ALU shortcut ([docs/66](docs/66_immediate_flags_experiment.md)), SUBS/CMP shortcut ([docs/71](docs/71_subs_cmp_experiment.md)), out-of-line `stepARM` no-flags dispatch ([docs/74](docs/74_steparm_noflags_dispatch_experiment.md)), inlined transfers in `ARM920T::step` ([docs/75](docs/75_step_loads_inline_experiment.md)), branch hints ([docs/76](docs/76_tick_step_checks_experiment.md)), and fixed-ratio loop specialization ([docs/79](docs/79_legacy_fixed_ratio_experiment.md)).
* **Audio Reference & Startup Analysis ([docs/84](docs/84_wade_title_audio_starvation.md)–[docs/90](docs/90_wade_music_single_buffer.md)):** Offline PCM verification against the digiBLAST XM soundtrack reference ([docs/88](docs/88_digiblast_soundtrack_reference.md)) and single-buffer startup DMA analysis ([docs/89](docs/89_wade_music_startup_delivery.md), [docs/90](docs/90_wade_music_single_buffer.md)).

</details>
