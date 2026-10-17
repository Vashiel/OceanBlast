# Compact Translation Cache Entries

## Change and decision

The address-translation cache now uses eight-byte entries instead of twelve-byte entries. Its 2,048 slots occupy 16 KiB instead of 24 KiB. One metadata word holds the virtual page, four access-permission fields, section/page fault classification and validity; the other word holds the physical page base. A hit combines validity and virtual-page identity in one comparison.

This implements the bounded hypothesis from the [current Wade profile](81_current_wade_profile_and_chefs_check.md). It preserves the existing permission, invalidation, table-walk and fallback policies. Architectural MMU limitations, including domain control, remain separate work. The earlier rejected validity-sentinel experiment did not make this layout change.

The candidate is retained after the targeted checks and one favorable Wade pair. No second pair or alternative layout was tried. The task allowance is 45 minutes, including compilation, comparison, documentation and the subsequent consolidated executable check.

## Correctness checks

All 16 MMU, 58 CPU, 87 timing and 12 UART checks pass. The six added MMU checks also pass against the original representation before candidate testing. They cover invalid entries at both virtual-address extremes, the highest virtual page, single/full invalidation, cached denial after descriptor edits, null fault reporting, every packed large-page permission combination across privilege transitions, and the existing synthetic kernel fallback. Existing checks cover small-page subregion permissions and section/page fault types.

The [metadata](validation/2026-10-17_compact_tlb_metadata.json) and individual check logs in `validation/2026-10-17_compact_tlb_*.txt` record the results. Production source differs from the measured candidate by one indentation correction only.

## One controlled Wade pair

Reference and candidate are normal GCC 16.2.0 native O3 unity builds with matching compiler options. The reference is the frozen executable used in the preceding profile; the candidate was built in an isolated source copy from `c71eb87`. This is not a normal-versus-PGO comparison.

Both runs use GUI/audio, disabled host pacing, ratio 6, 8.5 billion instructions, identical scripted input and a fresh zero-filled EEPROM. The measured fight section spans seven to 8.5 billion instructions. The script reaches the fight but supplies no further player actions inside that measured section. Both processes inherit affinity to logical processor 2. [Power observations](validation/2026-10-17_compact_tlb_power.csv) report AC power and the same Balanced scheme throughout. No compiler or second emulator runs concurrently with the pair; no profiler or screen capture is attached.

| Measurement | Reference | Compact entries |
| --- | ---: | ---: |
| Fixed-scene throughput | 79.6800 MIPS | 96.6638 MIPS |
| Scene CPU/bus host time | 18,668.5 ms | 15,334.9 ms |
| Complete-replay process CPU time | 107.0469 s | 91.8594 s |
| Complete-replay elapsed time | 120.7104 s | 99.5332 s |

The [raw comparison](validation/2026-10-17_compact_tlb_wade.csv) measures 21.31% higher fixed-scene throughput and 14.19% less complete-replay CPU time. Final CPU, SDRAM and framebuffer hashes are identical and match the established Wade state. These are observations from one pair on this host. They do not establish repeatability, gains in other games or performance on old CPUs, and must not be added to earlier reported percentages.

Absolute performance differs from the earlier controlled total comparison; only this pair is used for the layout decision. The unpaced runs report empty audio queues, so this measurement does not establish sound quality or full-speed playback. The consolidated PGO executable has a separate short acceptance check.
