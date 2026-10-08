# Game Cartridge Compatibility & CPU Execution Validation

Date: 2026-10-08. Baseline: `e2f9611`.

Scope: ARM/Thumb execution correctness, reproducible runtime diagnostics, and compatibility of the eleven local `roms/games/*.bin` cartridges. Verified menu or scene output is distinguished from sustained gameplay and synchronized audio; full compatibility remains unverified.

## Execution fixes

- Thumb register-offset memory instructions used the wrong format discriminator and load/byte bits. STR, STRH, STRB, LDSB, LDR, LDRH, LDRB and LDSH now decode independently and have individual regression checks.
- The Thumb PUSH mask also matched POP, making the POP branch unreachable. Separate masks now preserve the load/store distinction, with stack/return tests.
- ARM TST and TEQ updated N/Z but omitted shifter carry. Both now update C while preserving their source registers and V.
- ARM memory-offset RRX now uses CPSR carry rather than an unconditional zero.
- ARM single-word LDR/STR and SWP, plus Thumb register/immediate word transfers, now use aligned word accesses and load rotation instead of assembling bytes across two words. This is the alignment-check-disabled behavior; CP15 alignment checking and other transfer forms still need broader validation.

References used to check encodings and behavior: [GNU CGEN Thumb instruction descriptions](https://sourceware.org/cgen/gen-doc/arm-thumb-insn.html), [Arm's ARMv4T architecture description](https://support.arm.com/documentation/dui0471/i/key-features-of-arm-architecture-versions/arm-architecture-v4t?lang=en), and [Arm Using the Assembler, section 7.16](https://documentation-service.arm.com/static/5ea068ec9931941038de5e8e).

These are confirmed instruction-level defects. They did not fix the two original black-screen cartridges in the completed comparison runs. Do not attribute unchanged title behavior to these corrections.

The final alignment-fix audit repeats the 2-billion-instruction input experiment for every cartridge. All eleven final active frame dumps are byte-identical to the input-smoke run before the alignment fix; no endpoint-frame regression or compatibility improvement is inferred from that comparison.

## Reproducible diagnostics

`tools/audit_games.py` runs cartridges headlessly with an instruction budget, subprocess timeout, SHA-256 identification, separate logs/dumps and CSV/JSON observations. The output directory must be new to prevent replacing earlier evidence. Runtime duration with concurrent jobs is not a benchmark. `tools/render_audit.py` optionally decodes the saved active frames and timeline snapshots; it requires Pillow, which is not an emulator dependency.

`--snapshot-interval N` writes framebuffer/state snapshots every N executed instructions in either GUI or headless mode. State includes PC, registers, TTB, audio rate, DMA position and IIS/I2C registers. This shares the existing F7 snapshot path. `--input-script PATH` replays strictly increasing decimal instruction counts and hexadecimal button masks (bits 0–9). Comments start with `#`. Replay replaces live keyboard input for that session. `tests/start_buttons.txt` contains a generic Start/A/Right experiment, not a title-specific acceptance sequence.

Example from the repository root:

```powershell
python tools/audit_games.py --exe bin/oceanblast.exe --roms roms/games `
  --output C:/temp/oceanblast-audit-new --jobs 2 --timeout 180 `
  --steps 2000000000 --snapshots 100000000 --input-script tests/start_buttons.txt
python tools/render_audit.py C:/temp/oceanblast-audit-new
```

The optional `--match` filename glob selects cartridges; `--debug` enables guest diagnostics. Generic debug syscalls now include r0/r1/r2, making exit status and signal targets visible.

## Cartridge observations

The baseline uses 1.2 billion instructions with no injected input. The input experiment uses 2 billion instructions and snapshots every 100 million instructions. All eleven baseline runs reach Linux 2.6.11 and finish their instruction budgets without a detected kernel panic or timeout. A filled framebuffer proves neither correct execution nor working game controls/audio.

| Cartridge | Baseline active frame | After generic input experiment | Acceptance |
| --- | --- | --- | --- |
| Crazy Jack [G] (EN) | Almost black; 283/76,800 nonzero bytes | Still almost black | Unresolved |
| Cuccioli Cerca Amici [G] (IT) | Main menu | Save-slot selection menu | Gameplay/audio unverified |
| DigiQUAD [G] (EN) | Main menu | Racing scene | Sustained control/audio unverified |
| Gormiti Agguato nella Valle [G] (IT) | Arena title | Black active frame | Transition failure unresolved |
| Gormiti Lotta Oscura [G] (IT) | Intro scene | Title / Start prompt | Gameplay/audio unverified |
| Gormiti Masters of the Gorm Island [G] (IT) | Main menu | Level scene with character/HUD | Sustained control/audio unverified |
| Pitfall The Lost Expedition [G] (EN) | Forest scene | Title | Gameplay/audio unverified |
| Rayman 3 [G] (M10) | Intro scene | Title | Gameplay/audio unverified |
| Spider-Man Mysterio's Menace [G] (EN) | Main menu | Story scene | Gameplay/audio unverified |
| Superstar Chefs [G] (EN) | Almost black; 84/76,800 nonzero bytes | Still almost black | Unresolved |
| Wade Hixton's Counter Punch [G] (EN) | Publisher logo | Title / Start prompt | Gameplay/audio unverified |

Crazy Jack's debug trace includes guest `exit(0)` calls and LinuxThreads signal traffic before the shell waits again. Its black screen therefore needs investigation before the observed shutdown, not merely a claim that the final idle PC is a graphics hang. EEPROM failures also occur in cartridges displaying menus; they are not sufficient evidence of the cause. Superstar Chefs is black without the same EEPROM-open error in the baseline. The current I2C model remains incomplete and always NACKs transfers.

## Audio and validation

The Spider-Man GUI/audio/input experiment covers 2 billion instructions, approximately 100 seconds at the default limiter. Its 99 profile intervals average 19.999 MIPS, with approximately 19.04–20.89 MIPS individual intervals and zero counted dropped output samples. The game-time audio source settles at 22,050 Hz. These measurements do not prove audible quality, absence of underruns or correct audio/video synchronization. No universal "perfect audio" claim is made.

The optimized Windows build passes with `-Wall -Wextra`. CPU/DMA/input-replay, GPIO and streaming resampler suites pass. CPU regressions cover the eight Thumb memory operations, PUSH/POP, TST/TEQ carry and word alignment in addition to the previous cases. Source tests are ROM-free.

Summary CSVs are published under `docs/validation/` with cartridge hashes and execution budgets. ROMs, full memory dumps, proprietary frame images and executables are excluded from source publication.

## Unresolved Causes & Required Validation

1. Trace Crazy Jack and Superstar Chefs from asset loading to the first guest exit; inspect return values and game-specific video initialization before implementing a device workaround.
2. Reproduce Gormiti Agguato's black transition with a minimal title-specific button sequence and distinguish normal exit/loading from a fault.
3. Validate navigation, game start, sustained play and stop/restart separately for each of the eleven titles.
4. Measure actual underruns, audio queue latency and audio/video drift during gameplay; the dropped-sample counter alone is insufficient.
5. Complete board-device identification and I2C transactions using hardware/driver evidence, then validate saves and settings persistence.
