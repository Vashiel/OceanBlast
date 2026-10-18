# ARM multiple transfers and profiled build

Date: 2026-10-18. Continues [register returns](104_arm_register_returns.md).

## Delivered semantics

The shared frontend represents ordinary ARM LDM/STM transfers as one typed instruction. The initial subset excludes empty lists, PC bases and S-bit bank/exception-return forms. It includes all four addressing modes, writeback, stack transfers, PC stores and ordinary PC loads. PC loads end the translated region; failed conditions fall through normally.

The checked helper commits the preceding integer prefix before any partial transfer. Words execute in ascending register order with checked translation and existing physical bus access. A later-word abort preserves earlier results, suppresses final writeback and enters the same precise abort state. MMIO and code writes finish the current instruction and return before the next translated instruction. Memory accounting charges one guest instruction, not one instruction per transferred word. Existing reference alignment/base-overlap/PC-store behavior is preserved; this is not independent physical-hardware certification. See [Arm addressing mode 4 and LDM/STM descriptions](https://documentation-service.arm.com/static/5f8dacc8f86e16515cdb865a).

All six suites pass. Further boundary checks pass after adding a fixed-address stack save/return oracle, 896 load/store mode/register-list/predication combinations including all 16 registers, partial native-loop budgets, every tested legacy phase, late-word aborts, active-code writes and a device-touching multiple store. Unsupported forms remain explicit reference exits.

## Bounded Wade results

Each pair runs the same 2.4-billion-step title fixture, ratio 6, copied EEPROM, unchanged language, no title shortcut, affinity 4 and observed AC before/after. CPU, complete SDRAM and diagnostic snapshot hashes match exactly. GUI/audio/pacing are disabled for timing pairs.

| Separate pair | Reference CPU s | Candidate CPU s | Reference wall s | Candidate wall s |
| --- | ---: | ---: | ---: | ---: |
| Previous register-return build versus multiple transfers | 27.796875 | 26.187500 | 28.073 | 27.586 |
| Same multiple-transfer source, normal versus PGO | 28.437500 | 27.250000 | 29.369 | 27.999 |

These single pairs observe about 5.8% and 4.2% less process CPU time respectively. The first candidate has more host descheduling, and whole-run values vary between pairs. These modest observations are not a stable combined speedup, a comparison with the installed PGO interpreter, or weak-device acceptance. Generated work in the normal multiple-transfer pair rises from 573,384,083 to 588,101,592 steps; cached scalar hits fall from 8,398,023 to 2,821,254.

## Profile build and live gate

CMake now supports OFF/GENERATE/USE profile modes, with an explicit profile directory and errors for missing or mismatched profiles. Generation/use retain the same source and object paths. Only project targets are profiled; the pinned AsmJit library is unchanged. Prefer a short profile path on Windows. See [GCC instrumentation options](https://gcc.gnu.org/onlinedocs/gcc-15.1.0/gcc/Instrumentation-Options.html).

The first training attempt cannot write three files at the Windows path limit and is rejected. A single retry with a shorter output directory completes all six profiles. It uses the 2.4-billion-step Wade fixture with GUI/audio enabled, no pacing and a private zero-volume setting; CPU/RAM hashes match the reference. Training is not an acceptance measurement. The final PGO suites pass in 11.32 seconds.

PGO executable SHA-256: `6d2d182c4a320c7aa372eb501e207d2936d0d1cad90fa0432efaf4b1a696d3d2`.

The live watchdog stops this build after 9.76 seconds. Its last four intervals show approximately 77.0%, 75.0%, 75.4% and 59.1% modeled speed, with empty queues rising from 7 to 24. The window closes normally. No Chefs or gameplay sweep follows. The [manifest](validation/2026-10-18_multiple_transfers.json) records exact inputs, binaries, profiles, checks and failure.

The installed player remains unchanged. No usable test-EXE is ready. The next structural slice is a native execution window spanning already validated blocks, with safe successor lifetime/code checks and one shared device-event budget; more compiler-profile variants are not planned for this block.
