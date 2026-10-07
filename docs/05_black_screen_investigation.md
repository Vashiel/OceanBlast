# Black screen after loading: investigation

Date: 2026-10-07. Baseline: `c0e10cf`. Reported symptom: cartridge splash and loading screen appear, followed by a black screen when gameplay should begin. Existing untracked splash images and analysis scripts were preserved. No cartridge or extracted asset is included in these changes.

## Reproduction data

- Local test cartridge: 17,301,504 bytes.
- SHA-256: `5593f38adfe2159d18ad420301a6a21e73167291bb38e0e58a63c607c6bd5ba9`.
- Existing Windows w64devkit compiler, C++17, optimized build.
- Baseline and comparison commands: `oceanblast.exe <local-cartridge> --steps 800000000`.
- Extended diagnostic command: `oceanblast_final.exe <local-cartridge> --steps 1200000000`.
- Raw logs and dumps remain local in `./diagnostics/oceanblast-black-screen-20261007/`. Baseline dump files were produced in the parent working directory. These artifacts may contain proprietary guest memory and must not be published.

## Confirmed CPU defects and correction

Five new ROM-free tests failed on the baseline: immediate LSR/ASR zero encodings, RRX, ADC carry input with a shifted operand, and register ROR by 32. All seven prior tests still passed.

The fix distinguishes immediate shift encodings from register shift amounts. Immediate LSR/ASR encoding zero means a shift of 32; immediate ROR encoding zero means RRX. Register shifts of zero still preserve the value and carry. Nonzero ROR multiples of 32 update carry from bit 31. ADC/SBC/RSC now use the original CPSR carry as the arithmetic carry input, independently of the operand shifter carry output. Shifted memory offsets use immediate shift semantics too.

After correction, all twelve CPU tests passed. This is targeted validation, not complete ARM920T conformance.

Reference: [Arm shift specification](https://documentation-service.arm.com/static/5ed11a2dca06a95ce53f8f99).

## Confirmed DMA reload defect and correction

The CPU-corrected 800-million-step run progressed beyond keyboard-table queries into further game initialization, but repeatedly printed `dma2: loadbuffer:timeout loading buffer` and `dma2: timeout waiting for load`.

Channel 2 previously stopped at each completion even when automatic reload was configured. Its current transfer count was also read directly from the shadow DCON register, so queuing a second buffer altered the apparent active count. The correction latches the active count and reloads the next configured source/count before delivering the completion IRQ, unless NORELOAD is set. STOP clears the current count; final non-reloading completion clears channel ON.

Three synthetic DMA tests cover shadow versus current count, reload before IRQ, and NORELOAD completion. They passed alongside the CPU tests. In the repeated 800-million-step run, the two DMA timeout messages were absent. The model still uses a fixed 150,000-instruction transfer period; realistic IIS sample timing, partial transfers and audio underruns remain open.

Reference: [Linux S3C2410 DMA buffer handling](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/mach-s3c2410/dma.c). This adjacent kernel version is an implementation reference, not proof that the cartridge kernel source is identical.

## Corrected diagnostics

The old final LCD dump called virtual `bus.read32()` on physical MMIO addresses while the guest MMU was enabled. Its zero values were not evidence that LCD registers were unset. The dump now uses `getMmio()`, reports the active physical framebuffer and its nonzero-byte count, and saves `fb_active.raw` locally. The renderer itself is unchanged by this investigation.

## Validation and limits

- Optimized Windows emulator build succeeded with warning flags.
- Twelve CPU checks and three DMA checks passed.
- Existing keypad/GPIO suite passed.
- `git diff --check` passed.
- Baseline and two comparison runs completed at 800 million instructions.
- Guest output confirms SDL examining 240x160 modes and reports the guest framebuffer at 12 bpp.
- The repeated `0x4b46` ioctl is Linux KDGKBENT (keyboard table lookup), not a framebuffer/vsync wait. See [Linux keyboard ioctl definitions](https://github.com/torvalds/linux/blob/v2.6.12/include/linux/kd.h).
- ADC-labelled debug messages alone do not establish an ADC deadlock: the logged locations are revisited during subsequent execution.
- The extended run completed 1.2 billion instructions with final PC `0x000ef650` in user mode. LCDSADDR1 was `0x18150000`, selecting framebuffer PA `0x302a0000`; 57,467 of 57,600 bytes were nonzero. Neither of the two DMA load timeout messages occurred.
- Decoding `fb_active.raw` with the same RGB444 packing as the Windows renderer produced a recognizable game scene (background and character), saved locally as `final_frame.png`. This confirms scene generation beyond loading for the tested cartridge, rather than merely a nonzero buffer. Live Windows presentation, sustained gameplay, audible sound and actual host-key response remain unverified.

## Next handoff

Run the updated executable with the same cartridge and `--gui` to verify live presentation and control response. The framebuffer has a recognizable game scene after the corrections; avoid treating that single snapshot as complete gameplay acceptance. Retain the exact cartridge and run command when comparing changes. Further priorities are sustained rendering/input and realistic IIS/DMA timing. The corrected executable is installed locally at `bin/oceanblast.exe`; the prior binary is preserved in the diagnostic folder as `oceanblast_baseline.exe`.
