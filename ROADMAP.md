# Development Roadmap

Updated: 2026-10-18. The priority table defines completion criteria; delivered work and remaining validation appear under Current delivery plan.

## Committed execution architecture

The [accepted JIT architecture decision](docs/92_jit_architecture_decision.md) defines the development direction: shared ARM/Thumb translation frontend and IR, Windows x86-64 backend first, Android ARM64 next, with the interpreter retained as reference and fallback. A separate decoded-block interpreter and further discretionary interpreter shortcuts are no longer the delivery strategy. The J0-J6 sequence below supersedes the earlier P4 experiment gate; historical measurements remain unchanged. Initial native execution and checked memory are implemented; game speed and production acceptance remain open.

## Goal and priorities

Improve usable game performance on weaker CPUs while preserving the working cartridge compatibility. Keep the current interpreter as a reference and evolve the existing implementation in small, reviewable steps. The target is broad usability on old, low-end hardware: low single-thread throughput, modest memory and conservative CPU instruction requirements. An early second-generation Core i3 is an illustrative performance class, not a required device; weak ARM systems are also relevant. An exact device model is not a prerequisite for core optimization. This roadmap makes no minimum-hardware or universal full-speed promise.

Current measurements place most host cost in CPU execution and memory translation. This makes execution architecture a higher priority than frontend cleanup. Existing throughput and gameplay observations remain build-, host- and scene-specific; see [runtime acceptance](docs/50_runtime_acceptance.md), [host costs](docs/53_host_execution_costs.md) and [candidate experiments](docs/56_host_fast_path_candidates.md).

| Priority | Milestone | Completion criterion |
| --- | --- | --- |
| J0 | Shared engine/state boundary and precise exits | Interpreter, state handoff, interrupt and timing-phase checks pass. |
| J1 | ARM/Thumb decoder, typed IR and bounded block metadata | Focused semantics agree with the reference; unsupported cases exit explicitly. |
| J2 | Windows x86-64 code generation and executable cache | ABI-safe generated blocks execute through the machine boundary with measured translation coverage. |
| J3 | Memory, faults and code-cache invalidation | Checked access, alias writes, code changes and mapping changes cannot execute stale translations. |
| J4 | Whole-machine JIT integration and broader instruction coverage | Bounded boot/title/gameplay fixtures preserve state and event behavior without guest-time shortcuts. |
| J5 | Production Windows acceptance | Correct pacing, live audio and measured CPU reserve; interpreter fallback remains available. |
| J6 | Android ARM64 backend and device integration | Shared checks and real-device pacing/audio/reserve gates pass. A 32-bit ARM backend is separate work. |

The [architecture decision](docs/92_jit_architecture_decision.md) specifies contracts, scope and acceptance details. This is a commitment to the JIT execution path, not an open-ended engine experiment. Existing CPU-state, scheduler and MMU work is reused. A newly introduced boot, display or audio regression remains a release blocker; fixing one does not require restarting the architecture discussion.

## Current delivery plan

| Step | Status | Result or next acceptance gate |
| --- | --- | --- |
| JIT execution engine | J0 foundation and initial J1/J2/J3 slices delivered; J4 integration in progress | [Native coverage and sampled compilation](docs/103_native_coverage_and_sampled_compilation.md) adds ARM register shifts and halfword/signed access, with separately measured CPU-time gains of about 26.3% and 12.5% in matched title-reaching pairs. The final live gate still fails speed/audio requirements. [Ordinary ARM register returns](docs/104_arm_register_returns.md) now pass focused semantics and the matched title-state check. [ARM multiple transfers](docs/105_arm_multiple_transfers_and_pgo.md) also pass targeted/state checks, while their final PGO live gate still fails. [Delivery checks](docs/106_execution_window_delivery_gate.md) reject the short-budget cache candidate and find no advantage over the installed player in a corrected interpreter comparison. Next: validated native blocks sharing one execution window and device-event budget; no further compiler/profile sweeps. Test-EXE readiness and J5 acceptance remain open. |
| Resolve Chefs immediate life loss | Fixed and installed; short first-level check passed | [Invalid persisted difficulty](docs/91_chefs_eeprom_health.md) leaves player health at zero. Content-keyed launcher EEPROM files preserve the legacy image and repair only unsupported Chefs difficulty during migration. Short movement/jump replay retains five lives and reaches 100 points; full-game acceptance remains separate. |
| Resolve reported Wade title audio stutter | Open, P0 | [PCM comparison and listening](docs/85_wade_pcm_and_audio_recovery.md) reject the Wade-trained build and a two-block recovery candidate; preceding executable restored. [Local GBA comparison](docs/87_local_gba_wade_comparison.md) finds cross-port timing differences, but the [digiBLAST XM soundtrack](docs/88_digiblast_soundtrack_reference.md) matches generated PCM at approximately unity tempo. Neither is a console recording. Compare actual live output against generated PCM and trace queue/sample delivery before default-engine or release acceptance. |
| Compact translation-cache entries | Complete | [Eight-byte layout](docs/82_compact_tlb_entries.md), 173 targeted checks and one favorable Wade pair with matching state. |
| Consolidate the current executable | Complete locally | [Conservative x86-64 PGO build](docs/83_consolidated_pgo_acceptance.md), Wade state match and short paced/visible Chefs check. Previous executable backed up. |
| Check available old hardware | Open; suitable device unavailable in this session | Budget 10-15 minutes for setup and one affected-game check; record CPU, executable hash, settings, pacing and audio counters. Stop at the first relevant failure. |

Wade audio stability and actual weak-device evidence remain open gates. The earlier Chefs acceptance did not establish Wade audio quality. Core development follows the committed J0-J6 plan. Current runtime regressions remain release blockers; additional interpreter shortcuts and guest-time compression are not the performance strategy.

## Task budgets and decision rules

Break milestones into independently reviewable tasks. The following are default elapsed-time budgets per task, including investigation, implementation, build and validation; they are not estimates for an entire milestone.

| Task type | Total budget | Validation allowance within that budget |
| --- | --- | --- |
| Small fix | 15-30 minutes | 2-5 minutes |
| Bounded structural change | 45-60 minutes | 5-10 minutes |
| JIT performance check | 30-45 minutes | One short reference/candidate comparison, normally within 5-10 minutes |

Before starting, name the expected result, scope, relevant check and budget. Compilation and PGO training count toward that budget. Reuse a suitable existing baseline where its provenance is known; do not compare unlike compiler profiles and call the difference an implementation gain.

PGO preparation needs its own allowance inside the task budget: both compiler phases and cartridge training count. Do not automatically repeat multi-billion-instruction training for every small edit. If a build cannot finish within the remaining budget, preserve the current launcher and stop or agree on an extension; do not let compiler work silently consume the next task's time.

At the limit, stop and report one of: completed, rejected approach, or incomplete with a concrete blocker and proposed next slice. Ask the user before extending the agreed budget or starting a materially larger investigation. Do not silently add repeated comparisons. If the planned build or necessary check cannot fit, narrow the task or agree on a different budget before starting it. Never describe an untested result as accepted merely because time expired.

## Short checks first; stop on sufficient evidence

1. Run the smallest relevant technical check. CPU, MMU and timing changes require their targeted regression checks; documentation changes require document/link checks, not emulator builds.
2. For changes affecting runtime behavior, launch the affected cartridge with the appropriate executable and settings. Inspect the earliest screen that can answer the question. A title screen is sufficient when it already exposes the relevant pacing, rendering or audio problem.
3. **Stop immediately when the relevant failure is evident.** Do not wait for gameplay, finish an input script or collect more scenes after an obvious title-screen failure. Fix or reject the candidate first. Do not force gameplay progression when an earlier scene supplies sufficient evidence.
4. If the early check passes, advance only when a later scene tests something material that the early screen cannot cover. A title-screen pass does not establish gameplay, save behavior, long-run stability or audiovisual synchronization.
5. Stop after the predefined acceptance check passes unless a concrete new failure or unresolved question justifies another check. For a performance experiment, use one short comparable interval first. Repeat only to resolve a specific ambiguity and within the agreed budget. A small noisy difference remains inconclusive.

The contributor's short self-test is the primary routine acceptance check. When useful, ask the user whether the result feels better after reporting what was actually checked. User feedback is secondary and does not replace the contributor's basic verification. If local visible or listening checks are unavailable, state that limit and request only the missing observation.

Long gameplay sweeps, repeated benchmark pairs and broad cartridge coverage are reserved for a specific late-occurring problem, a justified shared-core risk, or release validation. Define their scope and time budget separately. Earlier research reports retain their historical methods; their long runs are not the default workflow for every new task.

## Existing foundation and historical implementation status (2026-10-17)

The P0-P5 rows below describe the delivered foundation and earlier experiments. They are historical status, not the active engine-selection plan; J0-J6 above governs new execution work.

The [first execution slice](docs/58_execution_boundary_and_short_checks.md) and [subsequent core changes](docs/59_machine_core_and_decoder_experiment.md) record the implementation and its limits.

| Priority | Delivered | Remaining |
| --- | --- | --- |
| P0 | Short technical/title-screen procedures and recorded configurations; broad old/low-end hardware goal | Add measurements on available weaker hardware when practical; exact model selection does not block core work. |
| P1 | Independent modeled CPU-state structure and shared single/batch interpreter boundary | Actual alternative execution engine and safe state handoff; current CPU still owns its state. |
| P2 | Scheduler owns legacy phase, register-clock fractions, device deadlines and idle advancement | Hardware timing calibration and finer scheduling remain open; register-clock mode is experimental. |
| P3 | Standalone MMU, independent checks, cached subpage rights and section-fault corrections | Domain-control, system/ROM protection and unsupported descriptor behavior need architectural review. |
| P4 | Decoder-cache prototype rejected after a slower pair; simpler [legacy phase batches](docs/60_legacy_phase_batches.md) retained after matching guest state and a faster short pair | A block execution engine or JIT remains unimplemented. PGO and weak-device gains are not established by the developer-build pair. |
| P5 | Shared machine owner used by the application and runtime probe | Frontend orchestration and other tools can be migrated when needed; no broad UI rewrite is scheduled. |

The [current ARM cost profile and direct immediate-operation path](docs/61_arm_dispatch_measurement.md) form the next retained execution slice. Diagnostic outlining, a TLB validity change and a broader register-operation shortcut were not retained after their short comparisons.

### Historical interpreter work

The following reports preserve the earlier interpreter work and its measured results. Recommendations quoted from those investigations have been superseded by the committed JIT plan. The [fixed Chefs active scene](docs/62_active_scene_acceptance.md), the interpreter reference and conservative distribution targets remain reusable validation assets. Android ARM64 is now a planned backend; actual device performance remains an acceptance gate.

The subsequent [MMU table-walk outlining experiment](docs/63_mmu_outline_experiment.md) passes targeted checks but produces no useful active-scene gain. The candidate was rejected; smaller generated code alone is not an acceptance criterion.

The [LDM RAM-span experiment](docs/64_ldm_ram_span_experiment.md) also produced no useful gain and was rejected. Before another memory candidate, profile costs and instruction frequency in the active gameplay section; the earlier startup sample does not establish that LDM translation is a material bottleneck there.

The [active-scene host profile](docs/65_active_scene_host_profile.md) now supplies the requested gameplay sample. It prioritizes a bounded direct path for flag-setting immediate ARM operations as the next hypothesis, subject to flag correctness and one active-scene comparison. No implementation gain is claimed from sampling.

The [flag-setting immediate extension](docs/66_immediate_flags_experiment.md) passes reference checks but loses throughput in its single active-scene pair and was rejected. The no-flags path remains unchanged. Before another special case, establish the eligible guest-instruction mix and account for the rejected extensions rather than using function-level sample share as a speedup prediction.

The [active-scene guest instruction mix](docs/67_active_scene_instruction_mix.md) counts exact opcode categories, basic blocks and short instruction sequences across steps three to four billion. The retained immediate no-flags shortcut hits on 22.93% of active steps, whereas the rejected flag-setting immediate subset and LDM transfers account for 3.20% and 3.07% (mean 2.75 registers). Unconditional/passing register-ALU operations with S clear (`REG_ALU_NOFLAGS`) form the largest remaining slice at 29.15% (14.94% unshifted, 14.16% immediate-shifted, 0.05% register-shifted), and two user-space loops account for 59.50% of all active steps. The next candidate tests a narrow flagless, carry-free register-ALU path (`S=0`, bit 4 clear, non-PC operands) against one active-scene pair.

The [register-ALU no-flags shortcut](docs/68_register_noflags_shortcut.md) passes all 58 CPU checks (with the constant-shift matrix expanded across all nine supported opcodes and both S states) and 87 timing checks, preserves identical CPU, SDRAM and framebuffer hashes in the active Superstar Chefs scene, and improves active-section throughput from 42.72 to 49.33 MIPS (+15.45%, whole-run CPU time -16.23%) in its single matched pair. The shortcut is retained; together with the immediate no-flags shortcut, direct flagless paths now cover 52.04% of active-scene instructions.

The [pre-indexed immediate word transfer shortcut](docs/69_word_transfer_shortcut.md) specializes non-PC, no-writeback `LDR`/`STR` word instructions with an immediate offset (8.76% of active-scene steps) at the entry of `executeSingleDataTransfer`. It passes all 58 CPU and 87 timing checks, preserves identical active-scene hashes, and improves active-section throughput from 42.56 to 50.60 MIPS (whole-run CPU time 63.88 s to 47.73 s) in its single matched pair. The change is retained; absolute wall-clock rates remain subject to host clock/power variance.

The [pre-indexed immediate halfword transfer shortcut](docs/70_halfword_transfer_shortcut.md) specializes no-writeback immediate `LDRH`/`LDRSH`/`STRH` instructions (the fourth-largest active-scene category at 7.38%, concentrated in the second-hottest user-space loop) at the entry of `executeHalfwordTransfer`. It passes all 58 CPU and 87 timing checks, preserves identical active-scene hashes, and improves active-section throughput from 55.10 to 57.91 MIPS (+5.11%, whole-run CPU time 69.97 s to 60.42 s, -13.64%) in its single matched pair. The change is retained; specialized paths now cover roughly 68% of active-scene instructions.

The subsequent [SUBS/CMP simple-ALU experiment](docs/71_subs_cmp_experiment.md) passes all 58 CPU and 87 timing checks and preserves active-scene hashes, but changes active-section throughput by only +0.74% (49.33 versus 49.70 MIPS), which is too small to justify retaining the extra branch. The candidate was rejected and reverted. A conservative `-O3 -march=x86-64` unity PGO build incorporating the three retained shortcuts (`9d3fea8f6a0bd04bc28bd77e4e560960b59685f8291d447c77a5fae9fe7a5f5c`) was installed locally to `bin/oceanblast.exe` (with the prior binary backed up) and verified against the 1.2-billion-step title check.

The [combined instruction-fetch check](docs/72_instruction_fetch_check.md) replaces the separate 1 KB subpage comparison and alignment test in `Bus::readInstruction` (which hits on 97.89% of active-scene instruction fetches) with a single bitwise XOR-and-mask check (`((va ^ instructionPage) & mask) != 0`). It passes all 58 CPU and 87 timing checks, preserves identical active-scene hashes, and improves active-section throughput from 51.92 to 55.71 MIPS (+7.31%, whole-run CPU time 66.58 s to 44.02 s, -33.89%) in its single matched pair. The change is retained.

The [data MMU and bus read/write fast-path streamlining](docs/73_mmu_bus_fastpath.md) simplifies the user-mode access-permission check in `Arm920TMmu::translate` to a single comparison (`ap <= (isWrite ? 2u : 1u)`), removes redundant `lastFault = MmuFault::NONE` stores on non-faulting reads and writes, and folds physical SDRAM range checks into single unsigned comparisons. It passes all 8 MMU, 58 CPU and 87 timing checks, preserves identical active-scene hashes, and improves active-section throughput from 54.53 to 65.87 MIPS (+20.80%, whole-run CPU time 53.97 s to 43.70 s, -19.02%) in its single matched pair. The change is retained.

The [step-to-stepARM no-flags dispatch experiment](docs/74_steparm_noflags_dispatch_experiment.md) tested moving `executeImmediateNoFlags` and `executeRegisterNoFlags` from `ARM920T::step` into the entry of `ARM920T::stepARM` under a shared top-6-bit mask. Because `stepARM` is out-of-line whereas both shortcuts are inlined directly in `step`, forcing the 52.04% flagless ALU slice through a `stepARM` call reduced active-section throughput from 57.96 to 54.60 MIPS (-5.80%). The candidate was rejected and reverted.

The [inlined word and halfword transfer experiment in `ARM920T::step`](docs/75_step_loads_inline_experiment.md) tested pulling the pre-indexed immediate word and halfword transfer shortcuts (16.14% of active steps) from `executeSingleDataTransfer` and `executeHalfwordTransfer` directly into `ARM920T::step`. Enlarging `ARM920T::step` with inlined memory-translation and frame-capture paths increased register pressure and reduced active-section throughput from 55.61 to 53.47 MIPS (-3.86%, whole-run CPU time +9.89%). The candidate was rejected and reverted; `ARM920T::step` retains only the two register-only flagless ALU shortcuts, while the word and halfword shortcuts remain at the entry of their transfer handlers.

The [`Bus::tick` and `ARM920T::step` branch-hint experiment](docs/76_tick_step_checks_experiment.md) tested removing the zero-cycle `usbHostEnabled` check before `if (!cycles) return;` in `Bus::tick` and adding `__builtin_expect` hints to rare exception/idle/thumb checks in `Bus::tick` and `ARM920T::step`. Active-section throughput was inconclusive (+0.13%, 51.58 versus 51.65 MIPS; whole-run interval throughput -1.64%), so the candidate was rejected and reverted.

The [post-shortcut active-scene host profile](docs/77_post_shortcut_host_profile.md) re-samples the active Superstar Chefs gameplay section with all five retained optimizations enabled. `ARM920T::stepARM` (down from 20.1% to 12.0%) and `executeSimpleDataProcessing` (down from 13.4% to 6.1%) have shrunk substantially, leaving per-instruction boundary overhead across `ARM920T::step` (26.3%), `InterpreterExecution::runLegacy` (8.9%) and `Bus::tick` (6.5%) at 41.7% of in-module samples. The next candidate targets the per-step `bus.tick(0)` overhead and legacy ratio batch loop.

The [zero-tick peripheral bypass](docs/78_zerotick_peripheral_bypass.md) eliminates calling `Bus::tick(0)` on zero-cycle ratio steps (two out of every three instructions at ratio 3) when no level-triggered interrupt source is active, reasserting UART0 TX level interrupts directly upon clearing `SRCPND` in `Bus::writeMmio`. It passes all UART, USB, 58 CPU and 87 timing checks, preserves identical active-scene hashes, and improves active-section throughput from 53.13 to 55.40 MIPS (+4.28%, whole-run CPU time 42.16 s to 39.41 s, -6.52%) in its single matched pair. The change is retained.

The [fixed-ratio legacy batch experiment](docs/79_legacy_fixed_ratio_experiment.md) checks compile-time groups for ratios 3 and 6 against scalar phase/IRQ behavior. Its Wade-first pair supplies no throughput gain, so the implementation was rejected without a Chefs follow-up or launcher rebuild. The generic loop and zero-tick bypass remain unchanged.

The [controlled total before/after comparison](docs/80_controlled_total_comparison.md) now validates the combined retained runtime changes (`6c7870f` versus `2880b0d`) with two reversed-order Wade pairs, fresh matching normal builds, observed AC/Balanced power and identical affinity. Both pairs favor current code, with mean fight throughput +32.13% and complete-replay CPU time -22.91%, while preserving final guest state. Use this current source as the next candidate baseline; do not add earlier single-pair percentages or apply this result to the installed PGO build or other devices.

The [current Wade profile and short Chefs counter-check](docs/81_current_wade_profile_and_chefs_check.md) complete the next diagnostic/compatibility slice without a rebuild. Wade matches the controlled reference state; Chefs reaches the active reference state at 60 MIPS with no empty queues or dropped samples. The next bounded hypothesis is compact TLB metadata with unchanged permissions/fault/invalidation behavior; the sample does not establish a gain or justify repeating the rejected loop specializations.

The [compact TLB implementation](docs/82_compact_tlb_entries.md) completes that hypothesis: entries shrink from 12 to eight bytes, preserving all 2,048 slots. All 173 targeted checks pass. One direct Wade pair preserves guest state and observes 21.31% more fight throughput with 14.19% less complete-replay CPU time. The change is retained; this single-pair result is not additive to earlier measurements or evidence for weak hardware. The next delivery is one consolidated conservative x86-64 PGO executable with short Wade-state and paced Chefs acceptance checks, followed by measurement on available old hardware. Further interpreter experiments require a new bounded hypothesis rather than an open-ended sequence of micro-optimizations.

The [consolidated PGO delivery](docs/83_consolidated_pgo_acceptance.md) completes the executable build and its bounded state/Chefs checks. Subsequent [Wade title feedback and diagnosis](docs/84_wade_title_audio_starvation.md) reproduce paced audio starvation. Additional Wade training reduces observed queue events, but listening feedback rejects its music output; that candidate is withdrawn and the preceding executable restored. Next: separate generated audio correctness from live delivery and resolve that P0 issue before another throughput experiment, then record a short check on available old hardware. Old-device evidence is still unavailable; no minimum-hardware claim is made. A future execution-engine experiment remains P4 work, with its own hypothesis and budget.
