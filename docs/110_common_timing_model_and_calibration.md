# Common timing model: audit, foundation and calibration plan

Date: 2026-10-19. Follows the [manual package and counter-checks](109_manual_jit_package_and_cross_game_checks.md). The agreed 30-minute slice establishes shared units, auditable timing reports and a reference-comparison procedure. Original-hardware calibration remains open.

## Decision

Timing correctness takes priority over the next native-block performance change. The target is one hardware timing model shared by the interpreter and JIT, respecting the clock and device registers written by any cartridge. Equal rules do not mean forcing every cartridge to a fixed frequency: guest clock-register writes remain meaningful.

The existing game-specific instruction ratios are historical compatibility settings. They are not independently established hardware requirements. Neither the 60/120-MIPS targets nor a displayed 100% model speed establish original-console tempo. Existing performance gains remain measurements of executing the same configured work faster; they do not validate that work's timing.

Do not replace all ratios with 3, enable the incomplete register-clock mode by default, retime music to a cross-port recording, or hide a failure by compressing guest work. Preserve the reference fixtures and the current installed/user-test EXEs while establishing the replacement model.

## Audit

| Area | Confirmed source behavior | Calibration status |
| --- | --- | --- |
| Internal units | 20,000,000 ticks represent one modeled second. | Unit choice; neither crystal frequency nor instruction rate. |
| Legacy execution | One device tick per selected number of CPU steps. Chefs/Pitfall use 3; Wade uses 6. Default host caps are 20 times the ratio in MIPS. | Empirical compatibility policy; uncalibrated. |
| Register-cycle execution | Estimated instruction cycles are converted using the selected CPU clock, with fractional time and clock changes preserved. | Incomplete cached-cycle model; uncalibrated. |
| Clock tree | Current board model assumes a 12-MHz input and initial crystal selection. Programmed values 0x52011/4/3 produce 180/90/45 MHz. | Register calculation has tests; board/reset assumptions are not new hardware measurements. |
| Timer and IIS/DMA | Device timing follows programmed PCLK/dividers and the shared internal units. | Register/device tests pass; complete hardware behavior is not established. |
| JIT with register timing | Current JIT delegates to the interpreter instead of generating cycle-accounted native blocks. | Now reported explicitly. |
| Cycle costs | Interlocks, cache/memory waits, some exception costs and accelerated FPA11 timing remain incomplete or provisional. | Must be reviewed before a calibrated CPU-work requirement is claimed. |
| Display and media | Presentation, game updates and audio delivery have different observations and latencies. | Host presentation FPS and continuous queues do not establish hardware animation speed. |

The [ARM920T manual, chapter 12](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb) gives cached instruction costs and load-use interlocks, and explicitly requires additional system-dependent costs for external accesses. It is not a complete board timing model. Its multiplier table/text discrepancy remains a source-review item; no new cycle constants are guessed in this slice. Clock-register and IIS sources remain collected in [23](23_register_clocks_and_iis_pause.md), with the earlier experimental-cycle limits in [24](24_automatic_timing_and_chefs_load.md).

## Implemented foundation

`EmulatedTime` is now the single runtime definition of the internal tick rate. CPU conversion, Timer 4, audio-DMA period calculations and default legacy host caps refer to it. Host pacing calculations are explicit and remain outside guest-state advancement. These changes preserve existing numeric behavior and presets.

Startup reports, snapshots and appended CSV columns identify `timing_model`, `timing_calibration`, `requested_engine`, `effective_engine`, `host_instruction_cap_mips` and `host_pacing`. Calibration is reported as `unverified`. A requested JIT with register timing reports the interpreter as effective backend. The window labels the percentage as model speed. Existing CSV field names/positions remain available; model speed still must not be read as console-speed acceptance.

## Short checks

Eleven native CTest suites pass in 17.29 seconds, including the existing CPU/JIT suites and explicit Timer 4, audio-clock and streaming-DMA checks. The new timing contract demonstrates:

| Workload | Model / old ratio | CPU steps | Internal ticks | Timer expirations |
| --- | --- | ---: | ---: | ---: |
| ADD plus backward branch | Estimated cycles / 3 | 90,000 | 20,000 | 1 |
| Branch loop | Estimated cycles / 6 | 60,000 | 20,000 | 1 |
| Branch loop | Legacy / 3 | 1,200 | 400 | 0 |
| Same branch loop | Legacy / 6 | 1,200 | 200 | 0 |

The first two rows use the implemented one-cycle ADD / three-cycle branch estimates at programmed 180 MHz. A Timer 4 configured at 45 MHz / 2 for 22,500 input counts expires at one modeled millisecond. This is an independent arithmetic consistency check of the implemented model, not a physical calibration. The legacy rows expose why instruction ratios are not host-only speed controls. An additional fixture confirms that register-cycle execution ignores the legacy ratio and that JIT native-step counts remain zero in that mode.

Two short 100-million-step reference/current pairs use Chefs legacy mode and Pitfall register mode. Canonical CPU and complete SDRAM hashes match in both. Snapshot/backend fields and CSV schema are checked; the Pitfall probe also exercises a populated CSV record. Chefs finishes before a one-second metrics row, so its CSV check covers the header and snapshot fields only. These startup checks do not accept gameplay, listening or a speedup. Wade is not executed.

The [contract observations](validation/2026-10-19_timing_contract.csv) and [validation manifest](validation/2026-10-19_timing_foundation.json) record exact identities and limits. Candidate SHA-256 is `c9f68a8acff43653daa4b408b0e128e44c955492c9711c05bf66e1d2e62130d3`. It remains an isolated developer artifact; installed and user-test EXEs remain unchanged.

## Video evidence and comparison procedure

Only the previously linked videos are available; no physical digiBLAST is available for a new measurement. The existing [Pitfall console-video reference](21_pitfall_original_hardware_reference.md) is a candidate for marking uninterrupted events after recording speed and edits are reviewed. The earlier rough screen timestamps are not frame-accurate calibration markers. [Wade's GBA comparison](87_local_gba_wade_comparison.md) is a different port, and the [XM soundtrack conversion](88_digiblast_soundtrack_reference.md) is not a recording of runtime execution. Neither can set digiBLAST CPU timing.

`tools/compare_timing_landmarks.py` compares at least three corresponding, increasing events. It uses intervals rather than power-on offsets, reports modeled/reference spans with declared marker-uncertainty bounds, and never writes presets or declares hardware timing calibrated. It rejects other platforms, converted soundtracks, missing provenance and ambiguous/nonmonotonic markers. Four focused tests pass, registered as a twelfth CTest and run separately in 0.12 seconds.

The reference JSON requires `platform: digiblast`, `kind: hardware_video`, `source_url`, `modeled_ticks_per_second` copied from the emulator snapshot, `reference_uncertainty_seconds`, and `modeled_uncertainty_ticks`. Event CSV columns are `event,reference_seconds,modeled_ticks`. Quantify both marker uncertainties from the actual capture and sampling interval; do not assume an illustrative value is measured. Declare the exact emulator model, cartridge revision, input/save state and capture method alongside observations. Source classification is supplied metadata, not automatic verification of recording authenticity.

```powershell
python tools/compare_timing_landmarks.py --reference reference.json --events events.csv --output comparison.json
```

No original-console event timings are collected or invented in this slice. The tool's synthetic unit-test markers are not reference measurements.

## Next gates

| Gate | Current status | Required result |
| --- | --- | --- |
| T0: Units, model identity and characterization | Complete | Shared runtime unit, explicit effective backend, consistent Timer/DMA tests and preserved reference state. |
| T1: Source-backed CPU/device timing contract | Open | Review instruction dependencies, faults, FPA11 acceleration, memory/cache costs and clock/reset assumptions. Separate established behavior from approximations. |
| T2: Original-video event comparison | Tool ready; observations open | Review an uninterrupted digiBLAST sequence, mark matching events with uncertainty, compare both modeled time and host delivery. Video evidence bounds conclusions; it does not certify full hardware equivalence. |
| T3: Cycle-accounted JIT integration | Open | Implement the same reviewed costs and shared timeline in generated blocks, with precise partial budgets, memory/fault exits and context transitions. Reference fallback remains explicit. |
| T4: Preset migration and performance acceptance | Open | Use the common accepted model for matched game checks before removing compatibility ratios or claiming real-time CPU reserve. No forced equal-ratio migration. |

Further standalone native-dispatch optimizations wait behind this timing work. The accepted JIT architecture remains the long-term execution strategy; the new priority establishes which guest work and time it must preserve.
