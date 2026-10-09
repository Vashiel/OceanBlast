# Runtime diagnostics and performance

`--cpu-steps-per-tick N` separates interpreter work from scheduled peripheral advancement. Default: 1; range: 1–16. The automatic GUI limit scales with this ratio unless `--clock-mips` is supplied. See [controlled timing probes](22_cpu_budget_and_runtime_timing.md) for reproduction, counter limitations and host-throughput results.

2026-10-07. The game window title now displays one-second measurements: **Anzeige FPS** (host display updates), **Bildwechsel/s** (sampled changes in the active framebuffer), **MIPS** (emulated instructions per wall-clock second), PC and physical framebuffer address. These are distinct measures: refreshing an unchanged screen is not a new game frame. Framebuffer changes are detected with a full-buffer FNV-1a hash at display refresh time. This can miss changes between samples and count partial renders; it is an estimate, not a guest-vsync frame counter. Splash/loading changes also count.

The host polls input every 10,000 instructions and schedules presentation using a monotonic clock at approximately 60 updates/s. The former mandatory 2ms sleep every 100,000 instructions is removed. Guest timers/DMA still use the existing instruction-based model; no claim of accurate emulated clock speed or 60 game FPS is made.

## Controls (focus the game window)

- F5: pause/resume CPU execution; window events and metrics remain responsive.
- F6: pause and execute one CPU instruction.
- F7: save `snapshot_<instruction-count>.raw` (240x160 packed RGB444) and `.txt` (registers, CPSR, PC, framebuffer). Works while paused too.

The launcher **FPS-Log** checkbox (on by default) adds `--profile` and records `performance.csv` with low overhead. **Debug-Log** adds `--debug`, enabling detailed hardcoded ARM probes, abort reports, and syscall logging. Guest writes such as repeated ALSA `snd_pcm_stop` messages are logged once per guest write, which can cause substantial disk output and slow play. Keep Debug-Log off while measuring normal speed. `--trace` also enables CPU probes and its initial instruction trace.

Profile mode adds `performance.csv`, one row per second in GUI mode, with interval duration, presentation rate, sampled change rate, instruction speed, PC and framebuffer. Addresses in CSV are decimal. Launcher files live in `bin/sessions/`; the CSV and session log are replaced each run, snapshots retain instruction-count names and may be overwritten on a subsequent run. These artifacts contain guest data and remain local. For comparisons use the same ROM, scale, sound setting and game scene, enable FPS-Log and leave Debug-Log off, then exclude boot/loading intervals.

Verification: build and CPU/DMA regression results are recorded after validation below. These features diagnose remaining performance and compatibility issues; they do not imply complete game compatibility.

Validation completed: optimized warning-enabled Windows build passed; all 15 CPU/DMA regression checks passed. A GUI session displayed FPS/MIPS/PC/FB values. F5 paused execution (0 MIPS); F7 produced framebuffer/register files. F6 followed by F7 advanced the recorded count from 279940000 to 279940001 and PC from c00b734c to c00b7350. Final CSV writes flush once per interval for live inspection. Gameplay frame rate has not yet been benchmarked; concurrent emulator execution prevents treating this functional check as an isolated performance benchmark.

Log review (2026-10-07): `bin/sessions/session.log` contains 19,592 repeated `snd_pcm_stop` lines. Inspection of the instrumented test shows these were captured from guest write syscalls because Debug-Log was enabled, not emitted by the host audio backend. The log has no performance CSV. The debug run produced 3.6 MB of logging by instruction 390 million, explaining why that run cannot be used to benchmark performance. FPS-Log is now separate and can be enabled without verbose syscall logging.

The test also logged unhandled ARM instruction `0xeca0420c` at `0x4000f300` and `0x400d1430`. Decoding the fields identifies an ARM coprocessor load (`LDC`) targeting coprocessor 2. The emulator currently takes the ARM undefined-instruction exception for it. Whether the game expects a coprocessor, intentionally probes for one, or has a fallback is unknown; blindly skipping it could change guest behavior, so no substitute was added.

Diagnostic overhead: abort and other CPU diagnostic messages (including coprocessor undefined-instruction messages) are emitted only with Debug-Log enabled. Exception handling is unchanged. Four 100-million-instruction headless runs averaged 3.38 versus 3.50 seconds; the short boot-heavy comparison shows no measurable difference. A useful gameplay comparison requires FPS-Log with Debug-Log off. Investigation of the `LDC` path and audio output quality remain open.

## Current Runtime Diagnostics

The [timer and runtime follow-up](17_timer_and_runtime_validation.md) adds audio submitted/queued frames and empty-queue observations to the title, snapshot state and profile CSV. Queued frames are a buffer-level upper bound; empty observations are not a complete audible-underrun count. Zero dropped samples does not establish synchronized playback.

The current event pump runs every 50,000 instructions and preserves brief press/release transitions. Host framebuffer polling only submits a redraw when image content, address, stride or format changes. Thus `present_fps` counts redraw submissions rather than all polling opportunities; static screens can report zero. Window repaint uses the cached decoded image.

`--pc-profile N` records guest execution-page samples in `pc_profile.csv` at snapshots and exit. Columns identify the translation-table base, virtual PC page and sample count. This optional diagnostic measures sampled guest execution, not native host profiling. Sampling is disabled by default.

`--display-format auto|lcd|rgb444|rgb565` defaults to `auto`; `lcd` disables cartridge compatibility selection. `--display-stride N` provides explicit host scanline diagnostics. F7 raw snapshots follow the effective format/stride recorded in their state file, rather than always being packed RGB444. Snapshot state includes cartridge CRC32, LCDCON1, automatic/explicit selection and compatibility activation. `--nvram PATH` loads and saves a strictly 2,048-byte device-settings image; the launcher defaults to `bin/sessions/board.nvram`.
