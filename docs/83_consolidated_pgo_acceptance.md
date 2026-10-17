# Consolidated PGO Executable Acceptance

## Delivered build

The local `bin/oceanblast.exe` now includes the retained interpreter/memory changes through the [compact TLB layout](82_compact_tlb_entries.md). The previous installed executable is preserved in ignored `build/launcher-backup-2026-10-17-before-consolidation/`. This closes the gap between current source and the older launcher that contained only the first three transfer/ALU shortcuts.

GCC 16.2.0 builds the executable with O3, unity compilation and conservative `-march=x86-64`, using the existing profile-generation/use script. Two headless training runs execute 900 million instructions each: Superstar Chefs and Rayman 3, both at legacy ratio 3. Profile use completes with missing profiles treated as errors. Training success is compiler evidence, not gameplay acceptance for Rayman 3.

The [build metadata](validation/2026-10-17_consolidated_pgo_build.json) records compiler profile, training/profile hashes, source-file hashes, previous executable and installed executable. Tested and installed SHA-256 is `57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`. This is a local executable update; no GitHub release package was uploaded.

## Bounded acceptance

Both checks run sequentially with GUI/audio, fresh settings, fixed input/EEPROM fixtures, logical processor 2 affinity and reported AC/Balanced power at the beginning and end. No compilation or sampling runs concurrently. Validation stops after these two checks; the full task, including the TLB experiment, takes about 15 minutes within its 45-minute allowance.

| Check | Scope | Result |
| --- | --- | --- |
| Wade | 8.5 billion instructions, ratio 6, unpaced, zero-filled EEPROM, existing fight replay | CPU, SDRAM and framebuffer hashes match the established reference. |
| Superstar Chefs | Four billion instructions, ratio 3, normal 60-MIPS pacing, FF-filled EEPROM, movement and A/B/C replay | 59.9707 MIPS, zero empty audio queues, zero dropped samples; all three guest-state hashes match. |

The stopped Chefs first-level image is visually inspected after execution ends: 760 points and four lives match the reference. Its window is then closed. [Wade metadata](validation/2026-10-17_consolidated_pgo_wade.json), [Chefs metadata](validation/2026-10-17_consolidated_pgo_chefs.json) and their companion interval CSV files retain the observations.

The unpaced Wade check reports 54 empty queues and 48,822 dropped samples. It establishes the recorded guest state, not audio quality or full-speed pacing. The paced Chefs queue counters are technical evidence; listening quality, audiovisual synchronization, extended play and saves are not accepted by this short check.

No old/new PGO performance comparison was performed, so no PGO speedup is claimed. The normal-build TLB comparison remains separate. Conservative x86-64 code generation reduces instruction-set requirements but does not demonstrate performance on a second-generation Core i3 or a weak ARM device. A short measurement on available old hardware is the remaining delivery check; no ARM executable or new platform port is included.
