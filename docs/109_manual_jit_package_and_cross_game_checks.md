# Manual JIT package and two-game counter-check

Date: 2026-10-19. Follows [shared native windows](107_shared_native_windows.md) and the [entry-ABI fixture](108_shared_native_entry_abi.md).

## Scope and package

The requested counter-check explicitly replaces Wade with Superstar Chefs and Pitfall. Wade is not executed in this session. The bounded task covers the separate user-test package and one short GUI/audio run per game; it is not another implementation or compiler-profile sweep.

The local package is `bin/JIT-Test-20261019`, with `OceanBlast-JIT.exe`, `Start-Chefs.cmd`, `Start-Pitfall.cmd`, a shared PowerShell starter and private per-game EEPROM/settings. It uses the exact previously compiled and validated shared-window candidate from a2190c4, SHA-256 `081269a368d1a68c05d1eac2358de02d39d96123d229a1e8b424225b24ac198a`. Recompiling unchanged runtime source is unnecessary. The isolated direct-jump ABI fixture is not integrated in this executable. The starter passes PowerShell syntax parsing. The installed player retains SHA-256 `858c64effd03fb43738d3bb12b1e98c98063b463d4f6d112293c907607942ab2`.

ROMs, saves, executable artifacts, screenshots and RAM remain local and ignored. The package references existing ROM files instead of copying them. Start files isolate settings and do not replace the normal installation.

## Checks

Both runs use the packaged executable, GUI/audio, legacy ratio 3, a 60-MIPS pacing target, affinity mask 4 and AC observed before/after. They stop automatically at their instruction limit, or earlier for sustained low speed with recurring empty queues. Chefs uses a copied, valid difficulty-1 EEPROM from the retained migration. Pitfall uses its own zero-filled 2,048-byte image. No run touches the normal saves. Scripted inputs include actions and movement, not only Start.

| Game | Checked extent | Last four modeled-speed intervals | Empty queues | Dropped samples |
| --- | --- | --- | ---: | ---: |
| Superstar Chefs | 1.6 billion steps; first-level movement/jump replay | 100.23%, 99.61%, 100.11%, 99.98% | 0 | 0 |
| Pitfall | 1.3 billion steps; title screen | 99.98%, 99.93%, 100.08%, 96.72% | 1 during startup, then unchanged | 0 |

Both exit normally. Chefs completes in 29.66 seconds wall time; the inspected final framebuffer shows score 100 and five lives. Its complete final SDRAM hash equals the previously accepted 1.6-billion-step [EEPROM-fix fixture](91_chefs_eeprom_health.md), including the same movement/jump input identity. This supplies state agreement without another reference replay. Pitfall completes in 29.47 seconds wall time. Its one queue-empty event appears near initial audio-rate switching and does not recur through the remaining check. The inspected final framebuffer shows the title screen, so Pitfall gameplay is not accepted from this run.

The metrics describe runtime pacing and queue transport. No listening-quality or physical-controller acceptance is claimed. These are short compatibility checks on this host, not a new baseline/candidate speedup or weak-device guarantee. Exact inputs, hashes, counters and raw CSV identities appear in the [manifest](validation/2026-10-19_cross_game_jit.json), [Chefs intervals](validation/2026-10-19_chefs_jit_live.csv) and [Pitfall intervals](validation/2026-10-19_pitfall_jit_live.csv).

## Wade dump question

The recurring starvation observed in the previous Wade live run is not reproduced by these two short counter-checks. That narrows the observed problem but does not establish a damaged Wade dump. Under current settings Wade targets 120 million guest instructions per second at ratio 6; both counter-checks target 60 million at ratio 3. Different instruction mixes and the higher execution requirement remain relevant. The successful counter-checks alone cannot distinguish a workload-specific execution problem, insufficient reserve, game-specific emulation behavior or damaged content.

The loaded Chefs and Pitfall byte counts, SHA-256 and CRC32 match the existing project identities. Those identities establish which local files were used; they are not independent dump verification. A confirmed second dump or independently verified known-good digest for the same Wade cartridge revision would be needed to assess corruption directly. No ROM replacement, patch or time compression is performed.

The separate EXE is available for the user's requested manual test. Production Windows acceptance remains open because the previous Wade failure and listening gates remain unresolved. The next runtime implementation remains integration of the tested shared-entry ABI and guarded native successor transfer.
