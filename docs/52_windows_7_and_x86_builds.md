# Windows 7 and x86 Compatibility Builds

## Platform Boundaries

The compatibility profile targets Windows 7 SP1 APIs and can also run on newer Windows. x64 and x86 packages must have distinct asset names and separate extraction directories. The x86 profile requires SSE2. A successful build or execution on Windows 11 does not establish Windows 7 support.

The standard installed launcher remains unchanged while compatibility candidates are tested. `-Install` writes x86 candidates to `bin/oceanblast-x86.exe` and legacy x64 candidates to `bin/oceanblast-legacy-x64.exe`.

## Compatibility Changes

`SetProcessInformation` is resolved at runtime rather than imported at startup. Its power-throttling adjustment is skipped when unavailable. The compatibility presenter resolves Direct3D and shader compilation dynamically, uses the Windows 7-compatible discard (blit) swap chain on all supported Windows versions. Missing libraries or unsupported devices retain the GDI fallback. The standard profile retains its previous graphics-library linkage.

Windows 7 rejects the high-resolution waitable-timer flag. The host pacer falls back to a regular waitable timer and requests 1 ms multimedia timer resolution for its lifetime; shutdown restores the timer-resolution request. Modern Windows retains the high-resolution timer path.

Instruction limits, cumulative execution counters, snapshot scheduling and probe scheduling use 64-bit values on both architectures. Batch lengths remain bounded host-sized values. The launcher window-enumeration callback has an explicit Windows calling convention; timer/DMA minimum calculations explicitly select 64-bit arithmetic where host pointer width previously affected template deduction.

## Build Profiles

Use a multilib MinGW-w64 compiler and resource compiler:

```powershell
& .\tools\build_profiled.ps1 -Roms @("path\to\cartridge.bin") -Steps 2100000000 -CpuStepsPerTick 3 -Optimization O3 -Unity -Architecture x86-64 -LegacyWindows
& .\tools\build_profiled.ps1 -Roms @("path\to\cartridge.bin") -Steps 2100000000 -CpuStepsPerTick 3 -Optimization O3 -Unity -Target x86 -LegacyWindows
```

Both compiler passes use the same architecture flags. x86 resources use `pe-i386`; profiles and test output remain in ignored `build/`. Do not reuse native x64 profile data for an x86 release.

## Validation Scope

The x86 timing/FPA11 and MP3-storage regression suites pass on the Windows 11 host. Initial GDI-only x86 and x64 PGO candidates each complete a five-billion-step Superstar Chefs GUI replay and reach the first level, with identical final framebuffer hashes. The x86 run crosses the 32-bit unsigned instruction boundary without terminating or wrapping the limit. These checks do not validate Windows 7 or hardware-equivalent game speed.

In those initial GDI-only runs, the measured intervals after two billion steps average 73.16% of the selected pacing target on x86 and 88.09% on x64. Audio empty-queue events total 1,164 and 185 respectively; both report zero dropped samples. These are host observations while a software-emulated VM is also running, not an isolated architecture benchmark. The dynamic-presenter candidates require separate runtime validation.

A software-emulated QEMU guest completes installation and reports Windows 7 Enterprise SP1 x64, version 6.1.7601. The current dynamic-presenter x64 candidate starts its launcher, opens Superstar Chefs and displays the cartridge boot screen. This verifies application startup and initial cartridge execution in that guest, not complete gameplay or audio quality.

The supplied installation media uses a Windows 10 PE setup environment and offers modified Windows 7 SP1 editions. The plain, unactivated x64 Enterprise entry was selected. These results cannot establish compatibility with every unmodified Windows 7 installation. x86 application tests in this guest exercise WoW64; they do not constitute an installation test on a 32-bit Windows 7 OS. The current x86 PGO candidate also starts its launcher and displays the Superstar Chefs cartridge boot screen in the x64 guest. The x86 timing/FPA11 suite reports zero failures after its required output directory is created. All 14 MP3-storage checks pass, including Unicode names, transactional import, rename, removal and refusal to edit storage while a guest writer holds it.

The x86 launcher suite passes with exit code zero after configuring a 1024 x 768 desktop with proper color depth. It covers default settings, embedded graphics, all settings categories, language persistence and stable controller repaint/highlight pixels. Initial attempts at 800 x 600 or with restricted color depth failed the pixel comparison; these display conditions are unsuitable for that test. Both current compatibility candidates also complete a headless Superstar Chefs run limited to exactly 100,000,000 steps, each with exit code zero. No audio-quality or real-time gameplay acceptance is claimed from software CPU emulation.

The same final x64 and x86 candidates each complete a 100,000,000-step GUI run on the Windows 11 host with exit code zero. This is a startup/execution smoke test, not complete gameplay acceptance.

## Embedded Display Regression

The initial compatibility packages could produce audio and successful DXGI presentation counts while the launcher LCD remained black on Windows 11. A same-ROM comparison showed visible output in the original release, black output in the dynamically loaded flip-chain compatibility build, and visible output with GDI. Presentation counts and a successful process exit therefore did not establish visible output.

The compatibility presenter now uses `DXGI_SWAP_EFFECT_DISCARD` with one buffer and `Present(1, 0)` rather than selecting the modern flip chain. This retains synchronized Direct3D presentation and optional-library/GDI fallback support. The standard graphics profile retains its existing flip-chain behavior. The observed failure is specific to the tested dynamically loaded flip-chain launcher path; its lower-level driver/runtime cause has not been established.

The visible-output regression check in `tests/windows_embedded_presentation.py` starts a cartridge through the real launcher and samples only the embedded LCD area on an unobstructed interactive desktop. The original compatibility x64 build fails with zero colored LCD pixels; the corrected PGO x64 and x86 builds show the Superstar Chefs title screen and pass. Rayman 3 also passes the visible-output check with corrected x64. Both corrected candidates complete another 100,000,000-step cartridge run in Windows 7 SP1 x64 with exit code zero. Process exit, presentation counters and framebuffer memory remain separate evidence.

## Tested Candidate Identity

The compatibility candidates use `O3`, unity compilation and architecture-specific PGO data. Both corrected compatibility profiles train on Superstar Chefs. The guest tests above apply to these executable hashes:

| Target | SHA-256 |
| --- | --- |
| x64 | `3b42bd3c3bd485f95d482790458d9b9c6f34557d79eb7a278003b69a76b9c212` |
| x86 | `e0d17f5fdf3555a8ee1cce540a517f2bcd566591e133f0eb7f923cec80fe5a0d` |

## Other Host Platforms

## Rayman 3 Audio and Host Contention

Sequential Windows 11 GUI/audio replays compare the published compatibility builds at ratio 4, with the same cartridge, input script (`tests/rayman_gameplay_confirm.txt`) and 4,800,000,000-step limit. The software-emulated Windows 7 VM is either paused or active. No builds or training runs execute during these comparisons. Mean speed covers samples from guest second 15 onward; empty-queue observations are cumulative over each run.

| Build | VM | Mean pacing speed | Minimum pacing speed | Empty audio queues | Dropped samples |
| --- | --- | ---: | ---: | ---: | ---: |
| Published x86 | Paused | 100.00% | 99.10% | 0 | 0 |
| Published x86 | Active | 89.87% | 73.60% | 161 | 0 |
| Published x64 | Paused | 100.00% | 98.28% | 2 | 0 |
| Published x64 | Active | 100.22% | 91.94% | 12 | 0 |
| Experimental x86, two-cartridge PGO | Paused | 100.11% | 98.15% | 5 | 0 |

The experiment reproduces x86 audio starvation under host CPU contention. Increasing audio buffers would not supply missing sustained CPU throughput. The two-cartridge PGO candidate does not improve queue stability and is not adopted. Prefer x64 on a 64-bit host and pause software VM tests during gameplay; retain the published x86 build and cartridge ratio.

All five final framebuffer hashes match (`6e9cd43540b45576bd05244ffa950ca0a93b1ffac568131c4343c062d7986248`). This supports equivalent final guest output, not listening acceptance or original-hardware timing. These bounded tests use standalone GUI execution, not the embedded launcher path, and do not establish complete Rayman compatibility. Queue observations are diagnostics rather than an audible quality score.

Raw metrics: [x86 paused](validation/2026-10-12_rayman_x86_release_audio.csv), [x86 active](validation/2026-10-12_rayman_x86_vm_active_audio.csv), [x64 paused](validation/2026-10-12_rayman_x64_release_audio.csv), [x64 active](validation/2026-10-12_rayman_x64_vm_active_audio.csv), [experimental x86](validation/2026-10-12_rayman_x86_mixed_audio.csv).

The CPU, memory and cartridge components are largely separate from Windows presentation. Non-Windows display and audio currently contain inactive stubs rather than usable gameplay backends. Linux and macOS releases need real window/audio/input implementations, a portable launcher/storage workflow, platform-specific packaging and runtime tests. No Linux or macOS release is currently validated.
