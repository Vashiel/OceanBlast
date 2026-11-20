# Changelog

This changelog records releases, architectural milestones, and empirical validation reports for **OceanBlast**. For a structured subsystem index, see [docs/README.md](docs/README.md).

---

## v0.4.2 — 2026-11-20 — IIS DMA Audio Fixes, Lower Audio Latency, Fewer JIT Interpreter Fallbacks & ARM920T Accuracy

* **IIS DMA Audio (`src/memory/bus.cpp`, `src/memory/bus.h`):**
  * The next audio buffer is latched when the guest driver writes `DCON` or polls `DSTAT`, at the latest after 32 items, and the waiting time counts as transferred. This fixes skipped buffers with two queued buffers (*Wade Hixton's Counter Punch*: wrong notes and stutter) and replayed buffers with single-buffer drivers at low CPU ratios (*DigiQUAD*, *Crazy Jack*: crackling).
  * Consumed PCM is copied once per 256-sample batch instead of one scheduled event per sample. A RAM write into the uncopied part of the active buffer copies the consumed samples first, so the output stays bit-identical. With sound enabled, the *Wade* fight start runs at 99.4% mean speed instead of 94.7%.
* **Lower Win32 Audio Latency (`src/audio/audio.h`, `src/audio/audio_win32.cpp`):** 1,024-byte buffers (11.6 ms), 32 buffers and a four-buffer preroll. The emulator-side delay of sound effects drops from a median of about 80 ms to about 41 ms.
* **x86-64 JIT (`src/cpu/translation_frontend.h`, `src/cpu/x64_execution_engine.cpp`, `src/core/execution_batch.h`, `src/core/interpreter_engine.h`):**
  * Data processing with `PC` as an operand (`add ip, pc, ip`, `mov lr, pc`) is translated natively; the *Wade* desert intro needs 13% less CPU time.
  * Privileged `MCR p15, c7` cache maintenance is translated as a no-op; wait-for-interrupt stays scalar.
  * Cold interpreter runs continue across branches and stop at the first branch target with native code. The interpreter share across 23 cartridges falls from 11–47% to 4–11%.
  * A full code cache evicts its oldest eighth per scan instead of searching the whole cache for every new block, and the player code cache grows from 8 MB to 32 MB. The *Gormiti Lotta Oscura* intro drops from 25 to 5 audio underruns.
  * Side-effect-free `CLKCON` word reads stay inside the existing device deadline, reducing the cost of the guest kernel's idle loop.
* **Host Pacing & Diagnostics (`src/core/host_pacer.h`, `src/main.cpp`):** Catch-up after a slow scene is limited to 16 ms. The title bar reports the effective execution engine and modeled speed. Audio diagnostics count produced frames and successful device submissions separately, and `--audio-capture-only` counts guest PCM without opening the playback device. [Timing and audio validation](docs/118_runtime_pacing_and_audio_delivery.md).
* **ARM920T Accuracy (`src/cpu/arm920t.cpp`, `src/memory/arm920t_mmu.h`, `src/memory/bus.cpp`) — [CP15 and transfer semantics](docs/119_arm920t_cp15_and_transfer_semantics.md), [Alignment and CP15 privilege](docs/120_alignment_and_cp15_privilege.md):**
  * `LDRT`/`STRT` in privileged modes use user-mode permissions under the DACR domain policy. Previously the kernel could write a signal frame into a read-only user page.
  * `LDM`/`STM` use the word-aligned address. *Spider-Man: Mysterio's Menace* executes two such transfers with an unaligned base about 590,000 times each in 1.5 billion steps and read or wrote shifted data every time.
  * With the CP15 `A` bit set, misaligned transfers take an alignment fault in the interpreter and in native code; the guest kernel handles it as on the hardware.
  * CP15 instructions in user mode raise Undefined Instruction. `MRC p15, 0, Rd, c0, c0, 1` returns the ARM920T cache type `0x0D172172`.
* **32-Bit NAND Reads (`src/memory/bus.cpp`):** NAND page offsets are computed in 64 bits. In the Windows x86 build, offsets beyond 4 GiB wrapped into the cartridge image, so the guest kernel marked NAND blocks 993 and 994 as bad and diverged from the 64-bit builds after about 127 million steps. v0.4.1 was affected as well.
* **Cartridge Presets (`src/core/cartridge_settings.h`):** *Gormiti Agguato nella Valle*, *Lotta Oscura* and *Masters of the Gorm Island* use CPU ratio 5. Ratio 3 decoded the *Lotta Oscura* intro video 17 s behind its soundtrack; ratio 6 cut the narration after the intro short.
* **Packages:** The Windows x86 executable is built with the same GCC 16.2 toolchain as x64 (PGO training on *Superstar Chefs*, *Rayman 3* and *Wade*) and links `msvcrt.dll` instead of the Universal CRT. Linux packages require GLIBC 2.38 and `libSDL2-2.0.so.0`, as in v0.4.1.
* **Validation:** Windows x64 passes 549 ROM-free checks and 13/13 JIT suites; Linux x64 passes 13/13 JIT and 19/19 portable suites; Linux ARM64 passes 19/19 portable suites under `qemu-aarch64`. *Superstar Chefs* at ratio 3 reaches identical CPU and SDRAM states on Windows x64 (JIT and interpreter) and Linux x64 after 600,000,000 steps, and on all four builds after 10,000,000 steps.

## v0.4.1 — 2026-11-19 — Async Win32 Audio Thread, Diagnostic Log Export, JIT Single-Step Fix & Clearer Title Bar

* **Asynchronous Win32 `waveOut` Audio Worker (`src/audio/audio_win32.cpp`, `src/audio/audio.h`, `src/audio/resampler.h`):**
  * Decoupled all WinMM calls (`waveOutWrite`, `waveOutRestart`, `waveOutGetPosition`) from the ARM emulation thread onto a dedicated event-driven worker thread (`CALLBACK_EVENT` + ring buffer) with one-time pre-prepared `WAVEHDR` buffers instead of per-chunk `waveOutPrepareHeader` / `waveOutUnprepareHeader` churn.
  * Added allocation-free in-place streaming resampling (`PcmResampler::processInto`, shared with `src/audio/audio_sdl.cpp`), reducing emulation-thread `writeSamples` time by **~7.9×** (`30.72 ms -> 3.87 ms` over 3 s, p95 `368.3 µs -> 19.6 µs`) and `getPlayedFrames` max latency from `321 µs` to `0.3 µs`.
* **Built-In Diagnostic Export (`src/display/diagnostics_win32.h`, `src/display/launcher.h`):**
  * Added **Export Diagnostic Log (`.zip`)** (dependency-free RFC 1951 Deflate ZIP writer), **Export Diagnostic Log (`.txt`)**, and **Upload Log to `paste.rs`** (via dynamically loaded `winhttp.dll`, with a large copyable result dialog and automatic clipboard copy) in both the **File** menu and **Settings → Timing & Diagnostics**.
  * Reports include OceanBlast build/arch, Windows version (`RtlGetVersion`), CPU/RAM, active audio device (`waveOutGetDevCapsW`), anonymized `session.log` (stripping `USERPROFILE` paths and sanitizing raw non-UTF-8 bytes), and summarized `performance.csv` metrics, readable live while a game is running.
* **JIT Single-Step / `--debug` Fix (`src/cpu/x64_execution_engine.h`, `src/cpu/x64_execution_engine.cpp`):**
  * Routed `X64ExecutionEngine::step()` directly to the reference interpreter instead of compiling 1-instruction JIT blocks via `runToBoundary(..., 1, ...)`, preventing code-cache memory blowup (~680 MB) and `<0.03 MIPS` stalls when `--debug` or single-step mode is enabled.
* **Clearer Window Title Bar (`src/main.cpp`):**
  * Replaced the internal `JIT-Test` string and confusing draw-FPS readout with a concise `OceanBlast v0.4.1 (x64|x86|arm64) | Speed 100% | LCD 60 Hz | Audio 22.0 kHz / Audio Off | VSync` bar while keeping full frame/audio metrics in `performance.csv` and diagnostic exports.

---

## v0.4.0 — 2026-11-16 — Cross-Platform x86-64 JIT, Shared Entry ABI, Per-Page TLB Generation, Flat MMIO & Linux/ARM64 Support

* **[Linux x86-64 JIT, ARM64 Linux & SDL2 Frontend](docs/117_linux_x64_jit_and_arm64_sdl2.md):**
  * Ported `X64ExecutionEngine` (`src/cpu/x64_execution_engine.cpp`), `jit/CMakeLists.txt`, and `tests/x64_entry_abi.cpp` to the POSIX / System-V AMD64 ABI (`RDI`/`RSI`/`RDX`/`RCX` helper marshalling, stack-spilled post-index writeback on slow/store paths, `/proc/self/maps` `r-x` verification alongside Windows `VirtualQuery`), and fixed LP64 integer literal deduction in `tests/x64_clocked_timing.cpp`.
  * Implemented portable SDL2 display, keyboard/GameController input, and audio backends (`src/display/display_sdl.cpp`, `src/audio/audio_sdl.cpp`) and added top-level `CMakeLists.txt` + `cmake/aarch64-linux-gnu.cmake` for ARM64 Linux (`aarch64`) interpreter builds and non-Windows CTest execution.
  * Updated Windows JIT release configuration in `jit/CMakeLists.txt` to target Windows 7–11 (`_WIN32_WINNT=0x0601`) with dynamic DXGI loading and conservative baseline `x86-64` codegen.
  * Verified Linux x86-64 JIT across all 13 CTest suites (`65.83 s` in WSL 1 Ubuntu 22.04) plus Wade 2.4B (`21.748 s`, `89.87%` native coverage), Chefs 600M (`5.420 s`), and Ubuntu 24.04 (`GLIBC 2.39`) compatibility, matching Windows canonical CPU/SDRAM SHA-256 hashes bit-for-bit. Verified ARM64 Linux across 19/19 portable CTests and Chefs 10M (`33e7097e...` / `a29f3c57...`) under `qemu-aarch64-static`.

* **[Per-Page TLB Mapping Generation, Compiled-Block Retention & Flat MMIO Table](docs/116_page_tlb_generation_and_flat_mmio.md):**
  * Replaced global `mappingGeneration` invalidation on single-page TLB flushes (`Bus::flushTlbEntry`) with a 2,048-entry per-4 KB page generation table (`pageMappingGeneration`) and filtered `setDacr` updates, cutting MMU mapping revalidations by **99.46%** on *Wade Hixton* (`6.90M -> 37.3k`) and **99.20%** on *Superstar Chefs* (`3.74M -> 30.0k`).
  * Retained already-compiled native blocks across sampling collisions in `sampledHotEntry()`, raising native JIT instruction coverage on *Wade Hixton* (2.4B steps) from **66.01% to 89.87%** (`sampled_fallback_steps` **-74.86%**) and reducing process CPU time by a further **17.83%** (**-25.39%** vs pre-115 `HEAD`).
  * Replaced `std::unordered_map<u32, u32> mmioRegs` in `Bus` with a `constexpr`-indexed flat `O(1)` S3C2410 MMIO register array (`22 × 256` words) and cached `VirtualQuery` verification per executable slab.
* **[Shared Native Entry ABI & Intrusive Dispatch Streamlining](docs/115_shared_native_entry_and_dispatch.md):**
  * Integrated the shared Windows x64 entry/exit bridge (`enterBridge`, `R12 = ARM920TState*`, `R13 = NativeRun*`, fixed 16-byte aligned 40-byte stack frame) and converted block codegen to single-pass `asmjit::x86::Assembler` with a 4-slot physical register cache, reducing peak code-cache memory by **9.8–11.7%**.
  * Replaced `std::unordered_multimap` code-cache indexing with a 4,096-bucket intrusive hash table (`cacheByPc`), eliminated per-hit `std::list::splice` pointer writes, and added 2-way guarded successor slots (`successors[2]`) with upper-bound `maxLimit` tracking, cutting slow window lookups by **67–77%**.
* **[Common Timing Model & Clocked Native Execution](docs/110_common_timing_model_and_calibration.md), [docs/113](docs/113_clocked_native_timeline.md):**
  * Unified CPU, Timer 4, and DMA Channel 2 scheduling under a shared 20-MHz peripheral time unit (`EmulatedTime`) and added opt-in cycle-accounted native JIT execution (`--engine jit --timing auto --jit-clocked`), verified across 13 CTest suites.
* **[Shared Native Execution Windows & Extended ARM Coverage](docs/103_native_coverage_and_sampled_compilation.md)–[docs/109](docs/109_manual_jit_package_and_cross_game_checks.md):**
  * Added native lowering for ARM register shifts, halfword/signed loads (`LDRH`/`LDRSH`/`LDRSB`/`STRH`), ordinary ARM register returns (`MOV pc, lr` / `BX`), `LDM`/`STM` multiple transfers, and shared native execution windows (`linkNativeWindows`), reducing JIT process CPU time by **31.4%** in matched 2.4B-step comparisons ([docs/107](docs/107_shared_native_windows.md)).

---

<details>
<summary><strong>Historical Development Log (2026-10-07 to 2026-10-18, Reports 00–100)</strong></summary>

## 2026-10-18 — Checked Memory and Developer Player

* **[Native Regions and Precise Exits](docs/100_native_regions_and_precise_exits.md):** Same-page direct links, register/flag retention, literal and shifted-offset memory, MUL/MLA, checked inline TLB reads, event-bounded suffix loops and direct warm-cache/scalar exits pass six suites and the 600-million-step Wade state comparison. The final live gate still fails pacing/audio; test-EXE readiness remains open.

* **[Cached Dispatch and Mapping Context](docs/99_cached_dispatch_and_mapping_context.md):** Added cached scalar exits, validated direct lookup, bounded event windows, relevant-state code identity, separate privilege/mapping checks, cold-block promotion, conditional accesses and bounded internal backedges. Six targeted suites pass. Runtime evidence and exact build limits are recorded separately; installed launcher unchanged.

* **[Mixed RAM Blocks](docs/98_mixed_ram_blocks.md):** Several supported RAM operations can share a generated block, with actual retired-step accounting and early exits for devices, faults and code-writing stores. Final bounded time accumulation passes six technical suites, including exact deadlines and PCM/DMA ordering. One intermediate Wade pair matched state but remained slower; the final build's Wade gate is pending. Installed/test-package executables unchanged.

* **[Native Memory and Invalidation](docs/97_checked_memory_and_player_integration.md):** Added checked ARM byte/word loads and stores, precise abort exits, direct ARM/Thumb branches, indexed bounded LRU reuse and physical code-page/mapping generations. Six targeted suites pass; a short Wade boot check matches canonical CPU state and complete SDRAM.
* **Separate JIT developer player:** Added the Windows frontend target, isolated test preparation, backend selection, counters and diagnostic checkpoints. Ordinary builds and the installed player retain their existing engine. The current JIT remains slower; integer prefixes now share a terminal checked memory access, but repeated memory boundaries still need consolidation. Title/gameplay, pacing, live audio and CPU reserve are unaccepted.

## 2026-10-17 — Current Wade Profile and Chefs Check

* **[Initial Native x86-64 Execution](docs/96_initial_x64_execution.md):** Added an optional pinned AsmJit developer target, native ARM/Thumb integer/flag/condition lowering, bounded source-validated code cache, scheduler deadline accounting and scalar fallback. The 4,580 semantic fixtures also execute generated code; targeted alias/permission/IRQ/cache tests remain ROM-free. Installed launcher unchanged; game performance and live audio are unaccepted.

* **[Shared Translation Frontend](docs/95_shared_translation_frontend.md):** Added bounded host-independent ARM/Thumb integer blocks with explicit carry, condition, flags and pre-execution fallback representation. Unsupported memory/control operations stop before execution.

* **[Shared State and Execution Boundary](docs/94_shared_state_execution_boundary.md):** Machine-owned CPU state, a generic batch backend contract, handoff without resetting timing, and explicit reference exits for deadlines, context changes, waits, interrupts and exceptions. Added ROM-free boundary/dispatch checks; no speedup or audio acceptance is claimed.

* **[Planning Retrospective](docs/93_personal_planning_note.md):** Personal note on defining performance goals and choosing an execution architecture before a prolonged sequence of local optimizations.

* **[Committed JIT Architecture](docs/92_jit_architecture_decision.md):** Adopted shared ARM/Thumb block translation and IR, Windows x86-64 first and Android ARM64 next. The interpreter remains reference/fallback. Replaced the alternative-engine experiment gate with J0-J6 deliverables, precise timing/invalidation requirements and combined pacing/audio/CPU-reserve acceptance. This is an architecture decision; no JIT runtime is delivered yet.

* **[Chefs EEPROM and Health](docs/91_chefs_eeprom_health.md):** Reproduced immediate life loss with unsupported persisted difficulty 80, which leaves player health at zero. Launcher saves are now separated by cartridge contents; first migration preserves the shared legacy file and changes only invalid Chefs difficulty to 1. Added targeted storage/migration checks. Existing local title-blit work is preserved.

* **[Wade Single-Buffer Music Startup](docs/90_wade_music_single_buffer.md):** The ratio-6 capture loops one 1,024-byte DMA buffer until music second 1.85, then alternates both ring halves. The identical replay is about 16 ms at that handover. The rest of the mismatched opening is freshly written. Ratio 12 still leaves the first second wrong and would speed the fight, so no preset, DMA or playback change is installed.

* **[Wade Music Startup Delivery](docs/89_wade_music_startup_delivery.md):** The title-music mismatch is already in offline PCM. Delivery gaps stay under 1 ms and the IIS rate does not change at music onset. Extra guest CPU shortens the bad interval without removing its start. One later DMA period rereads 16 ms of unreplaced audio.

* **[digiBLAST Soundtrack Reference](docs/88_digiblast_soundtrack_reference.md):** The supplied video is an XM asset conversion, not a console recording. Its musical structure matches generated OceanBlast PCM near unity tempo, so the GBA difference does not justify a tempo correction. Live output stutter and startup differences remain open; no runtime change is made.

* **[Local GBA Wade Comparison](docs/87_local_gba_wade_comparison.md):** Captured the supplied GBA port in isolated mGBA. START blinks 4.6% faster in OceanBlast; corresponding generated music structure advances about 8.7% faster, with consistent split-window fits and no matching uniform pitch increase. Cross-port timing and recurring live stutter remain unresolved; no runtime change is made.

* **[GBA Reference Analysis](docs/86_gba_title_reference.md):** Measured START cycles from the supplied playthrough and the restored emulator: 0.5358 versus 0.5122 seconds, using the source stream's exact frame rate. Created local waveform/spectrogram references; weak cross-version audio matches do not justify a tempo correction. No timing change was made.
* **[PCM Comparison and Rejected Recovery](docs/85_wade_pcm_and_audio_recovery.md):** Three-billion-step probes produce identical raw/converted PCM before and after the retained core shortcuts. A two-block recovery candidate passes device checks but sounds worse in direct feedback, so its audio/test changes are removed. The previous executable remains installed; correct live audio is still unaccepted.

* **Wade Audio Rollback:** Listening feedback rejects the Wade-trained PGO candidate because music remains severely incorrect/stuttering. The preceding local executable is restored. Lower queue counts are retained as historical observations and do not establish correct music or acceptable playback.

* **[Wade Title Audio Diagnosis](docs/84_wade_title_audio_starvation.md):** Reproduced reported stutter with repeated empty audio queues below the 120-MIPS target. Additional Wade PGO training preserves guest state and reduces observed queue events to six, versus 138 and 25 in surrounding previous-build runs. Host variability and remaining startup events exclude complete audio acceptance; the issue now precedes further throughput work in the roadmap.

* **[Consolidated PGO Executable](docs/83_consolidated_pgo_acceptance.md):** Rebuilt the local launcher from current source with conservative x86-64 PGO and preserved the preceding executable. Wade retains the established guest state; the short active Chefs replay averages 59.97 MIPS with matching state, zero empty queues and zero drops. The stopped gameplay image is checked. This is bounded local acceptance, without an old/new PGO performance claim or a new release upload.

* **[Compact TLB Entries](docs/82_compact_tlb_entries.md):** Packed translation metadata reduces the 2,048-entry array from 24 to 16 KiB. All 173 targeted checks pass; one controlled Wade pair preserves final guest state and measures 21.31% more fight throughput and 14.19% less complete-replay CPU time. The layout is retained, with single-pair and host-specific limits recorded.

* **[Current Wade Costs](docs/81_current_wade_profile_and_chefs_check.md):** Reused the validated executable for one matched-symbol fight sample. MMU translation remains a material sampled cost; compact TLB metadata is the next proposed experiment, with no gain assumed. A subsequent short Chefs replay holds 60 MIPS with matching guest state, no empty queues and no dropped samples. No runtime or launcher change was made.

## 2026-10-16 — Fixed-Ratio Batch Experiment

* **[Controlled Total Comparison](docs/80_controlled_total_comparison.md):** Fresh pre-optimization and current normal builds were compared in two reversed-order Wade pairs under observed AC/Balanced conditions and identical processor affinity. Both pairs favor current code; means show 32.13% more fight throughput and 22.91% less complete-replay CPU time with matching guest state. These are scene/host-specific combined results, not additive validation of earlier individual percentages. No launcher was replaced.

* **[Legacy Batch Specialization](docs/79_legacy_fixed_ratio_experiment.md):** An isolated ratio-3/6 unrolling candidate passes 58 CPU and 90 timing checks, including every initial phase and IRQ return coverage. Its one Wade fight comparison preserves guest state but supplies no throughput benefit, so the candidate was rejected. The generic loop, prior optimizations and installed launcher remain unchanged.

## 2026-10-15 — Prioritized Roadmap and Bounded Validation

* **[Immediate-ALU Flag Extension](docs/66_immediate_flags_experiment.md):** Expanded direct immediate execution to flag-setting operations in isolation. CPU reference and timing checks pass and gameplay state matches, but the single active-scene pair is slower. The extension was rejected; the retained no-flags shortcut and installed executable remain unchanged.

* **[Active-Scene Host Profile](docs/65_active_scene_host_profile.md):** A bounded sample of the current gameplay replay identifies ARM execution/dispatch as the largest resolved source group. Final guest state matches the reference. The next proposed candidate is a narrow flag-setting immediate-ALU path; the profiling task changes no execution code or installed binary.

* **[LDM RAM-Span Experiment](docs/64_ldm_ram_span_experiment.md):** A candidate translates consecutive LDM RAM reads once inside a single 1-KB rights region. It passes targeted partial-abort, boundary and addressing checks, but its one active-scene pair shows no benefit. The candidate was rejected; production code and the installed executable remain unchanged.

* **[MMU Outlining Experiment](docs/63_mmu_outline_experiment.md):** An isolated table-walk outlining candidate passes MMU, CPU and timing checks and preserves the active gameplay state. Its one comparison shows no useful throughput gain, so the patch was rejected. Production source and the installed launcher remain unchanged.

* **[Unpaced Active-Scene Comparison](docs/62_active_scene_acceptance.md#unpaced-throughput-follow-up):** The immediate-ALU path reaches 82.50 versus 72.22 MIPS in one matched Superstar Chefs gameplay section, with identical final guest state. Whole-process CPU time decreases by 9.15%. This is one normal-build pair on the current host; unpaced audio drops exclude sound-quality acceptance, and no installed binary was replaced.

* **[Active-Scene Acceptance](docs/62_active_scene_acceptance.md):** Added a short first-level replay with movement and A/B/C. One matched pair preserves CPU, memory and framebuffer state; both builds hold the same pacing cap without empty queues or dropped samples. Lower section execution time is recorded as a single observation, not a general gameplay gain. GUI comparisons now have time limits, stopped-scene inspection, early-close rejection and optional audio limits.

* **[ARM Dispatch Measurement](docs/61_arm_dispatch_measurement.md):** A short current-build profile identifies CPU execution as the main host cost. Plain immediate ARM operations can now bypass the general decoder while preserving the existing interrupt, fetch and peripheral boundaries. The CPU suite has 58 passing checks; timing and short title checks pass. One developer-build pair improves from 76.076 to 79.401 MIPS. Three other candidates were not retained; no general speedup is claimed from the single pairs.
* The local PGO launcher now uses conservative `x86-64` code generation. Its short title check holds 60 MIPS with zero empty queues or dropped samples and matching final SDRAM/framebuffer state; the preceding launcher is backed up. This is local functional acceptance, not a measured PGO speedup or a new release package.
* **[Legacy Phase Batches](docs/60_legacy_phase_batches.md):** Grouped legacy execution between peripheral ticks while preserving per-instruction interrupt/fault checks. The isolated candidate passes 87 timing checks and retains identical guest state in one short pair, with throughput rising from 76.039 to 83.718 MIPS. This is a developer-build observation, not a general PGO or weak-hardware performance claim.
* Rebuilt the local native x64 PGO launcher and retained its predecessor as a backup. One short title-menu check holds 60 MIPS with no empty queues or dropped samples. The performance goal is broad usability on old, low-end hardware; no specific device or new platform port is required for the next core optimization.
* **[Machine Core and Decoder Experiment](docs/59_machine_core_and_decoder_experiment.md):** Extracted modeled CPU state, centralized execution scheduling and moved translation/TLB responsibilities into a standalone MMU. Corrected cached subpage rights and section fault types in a separate change. The application and runtime probe now share a machine owner. Targeted checks pass; a short ownership comparison preserves guest state and pacing. A limited decoder-cache candidate was slower in one short pair and was rejected. One observed GUI audio-queue stall remains recorded separately.
* **[Interpreter Execution Boundary](docs/58_execution_boundary_and_short_checks.md):** Single steps and batches now share legacy-ratio and register-clock advancement through `InterpreterExecution`. The developer build, 76 timing checks, 55 CPU checks and one short visible Superstar Chefs title run pass. CPU-state separation remains open; no performance gain is claimed.
* **[Development Roadmap](ROADMAP.md):** Added ordered architecture milestones for weaker-CPU performance, with explicit goals, rationale and completion criteria. Defined per-task time budgets, short contributor self-tests as the primary routine check, and immediate termination when the relevant failure is already evident on an early screen. Longer validation requires a concrete reason and a separate budget. This documentation update makes no new runtime or performance claim.

---

## 2026-10-14 — TLB Cold-Fault and Large-Page Checks

* **[TLB Permission-Tag Candidate Checks](docs/57_tlb_permission_tag_candidates.md):** The fourth isolated candidate preserves existing TLB entries on denied cold translations. Additional ROM-free probes cover colliding entries and all sixteen large-page slices across privilege transitions. The targeted diagnostics and CPU/timing/UART suites pass; performance and gameplay acceptance remain separate checks.
* **[Runtime Build Profiles](docs/46_runtime_build_profiles_and_rayman_ratio.md):** Shortened GCC counter filenames in deep Windows project paths using the same project prefix during profile generation and use. A bounded compiler check verifies that the profile is saved and loaded successfully.

---

## 2026-10-13 — Frontend Preferences and Host-Cost Validation

* **[Frontend Settings Persistence](docs/55_frontend_settings_persistence.md):** Focus and dropdown-open notifications no longer save settings or reload the embedded game layout. Saving another option preserves the preferred controller when a temporary fallback device is active. Regression checks cover notification filtering, separate controller mappings and actual audio edits.
* **[Host Fast-Path Candidate Validation](docs/56_host_fast_path_candidates.md):** Isolated candidate comparisons preserve guest-state evidence and distinguish timing variation from a repeatable throughput gain. GUI comparisons support reversed repeated pairs, isolated frontend preferences, process CPU time and a fixed-scene cost interval.
* **[TLB Permission-Tag Candidate Checks](docs/57_tlb_permission_tag_candidates.md):** ROM-free probes identify cached-permission, subpage and cold/cached translation inconsistencies. A corrected isolated candidate passes the bounded probes and CPU/timing/UART regressions; production adoption and throughput improvement remain unverified.
* **[Host Execution Costs](docs/53_host_execution_costs.md)** and **[CPU Sample Profile](docs/54_host_cpu_samples.md):** Measured ARM execution, address translation and peripheral work dominate the tested Wade and Superstar Chefs scenes; GUI work is a small fraction of the measured main-thread time.

---

## v0.3.0-alpha — 2026-10-12 — MP3 Storage and Player Integration

* **[Windows 7-11 Compatibility Builds](docs/52_windows_7_and_x86_builds.md):** Added separate x64/x86 packages and corrected black launcher LCD output in the initial compatibility builds by using the synchronized DXGI blit chain. Visible embedded-output regression checks cover Superstar Chefs on both architectures and Rayman 3 on x64. Windows 7 SP1 x64 validates application startup and bounded execution; native 32-bit Windows and hardware-equivalent gameplay remain unverified.
* **[MP3 Storage and Original Player](docs/51_mp3_storage_and_player.md):** Added an opt-in OHCI/USB mass-storage device backed by a separate writable image. First MP3-cartridge launch offers a 256-MB FAT16 store and music import; **Settings → MP3 Storage...** provides import, rename, deletion and free-space reporting. Transactional edits preserve the existing image on failure and refuse concurrent guest writers. The original player boots with the attached store; a synthetic MP3 test tone has bounded PCM evidence, while broader decoder compatibility remains experimental.
* Added MP3 player and storage-manager previews to the project homepage, with the complete workflow and limitations linked above. Windows release builds use an optimized PGO unity build targeting baseline x86-64; local developer profiles may still use `-march=native`.
* Known video-only cartridge identities enable native complete-frame capture independently of the selected CPU ratio. Combined game/video cartridges retain the existing guest-metadata probe. Windows audio diagnostics now distinguish played frames, remaining submitted frames and pending samples; these metrics do not themselves correct audiovisual synchronization.
* Corrected the Rayman background classification after the supplied original-console recording showed a similarly magenta sky. This is a reference observation, not a palette change or hardware-speed claim.

---

## 2026-10-12 — Runtime Build Profiles & Rayman 3 Performance

* **[Controller Settings Repaint](docs/47_controller_settings_repaint.md):** Buffered the animated controller illustration and excluded child controls from parent drawing; launcher checks include repeated pixel-stable controller repaints and highlight restoration.
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

</details>
