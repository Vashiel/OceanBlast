# Guest Time and LCD Wait Diagnostics

## Questions and Time References

The [Wade first-fight recording](37_cpu_fetch_and_interpreter_execution.md) reaches approximately 99% of its configured 80-MIPS pacing target while remaining visibly slow. That percentage establishes instruction delivery against an emulator setting, not original-console speed. A low guest instruction allowance, delayed guest timekeeping and active register polling require separate measurements.

The recording uses **legacy instruction-ratio mode at 4x**, not register-clock cycle mode. One modeled peripheral second contains 20 million bus ticks and 80 million CPU step calls. Host elapsed time is a separate reference. Jiffies must be observed alongside both references: using jiffies as the sole definition of an emulated second would conceal lost guest timer ticks.

## Kernel and Controller References

The exact [Linux 2.6.11 source archive](https://cdn.kernel.org/pub/linux/kernel/v2.6/linux-2.6.11.tar.bz2) defines `HZ=200` in `include/asm-arm/arch-s3c2410/param.h`. Its `arch/arm/mach-s3c2410/time.c` selects Timer 4, calculates `(PCLK / 6) / HZ`, then subtracts one from the count. At the observed 45 MHz PCLK, `TCFG0=0x200`, `TCFG1=0`, and `TCNTB4=0x927b` therefore agree with a 200-Hz reload period. This upstream source is a reference, not the complete cartridge vendor kernel. The archive does not contain an S3C2410 framebuffer driver implementation, so it cannot establish the cartridge driver's ioctl behavior.

Samsung's S3C2410A manual confirms these TFT controller fields:

| Register | Address | Relevant fields |
| --- | --- | --- |
| [LCDCON5](https://laundry.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=396) | `0x4d000010` | Read-only VSTATUS `[16:15]` and HSTATUS `[14:13]`; sync, back porch, active and front porch phases |
| [LCDINTPND](https://laundry.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=403) | `0x4d000054` | Frame-synchronized pending source at bit 1 |
| [LCDSRCPND](https://laundry.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=403) | `0x4d000058` | Frame-synchronized source-pending bit 1 |
| [LCDINTMSK](https://laundry.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=404) | `0x4d00005c` | Frame-sync mask bit 1; reset mask value `0x3` |

OceanBlast currently stores these LCD register values without a guest scan-phase or frame-sync interrupt source. Host presentation VSync and coherent framebuffer capture do not supply that missing guest peripheral behavior. This is a hardware-model limitation, but it does not establish that Wade waits for it. Generating a guessed periodic interrupt before observing the guest wait path would change behavior without identifying the reported bottleneck.

## Instrumentation

`tools/scene_work_probe.cpp` retains its existing command-line arguments and adds:

| Output | Meaning |
| --- | --- |
| `timing.csv` | Modeled bus ticks, CPU steps, host elapsed seconds, independently inspected jiffies, timer expiration/request totals, requests while the source was already pending, IRQ-entry observations and separate source/selected acknowledgements |
| `mmio.csv` | Per-modeled-second guest reads/writes by physical register, with one host-inspected value at the interval boundary |
| `top_pcs.csv` | Ten most frequent sampled `(TTB, PC)` pairs per modeled second, including each interval's total sample count |
| `syscalls.csv` | Userspace `open`, `execve` and `ioctl` arguments at SWI entry; OABI immediate numbers and zero-immediate EABI `r7` numbers are distinguished |

PC sampling occurs every 1,009 step calls. It estimates observed work distribution, not exact retired-instruction percentages. Fixed-stride sampling can alias a loop. The existing `estimated_cycles` field sums only sampled instruction cycle estimates. Non-user modes are grouped as kernel observations, and IRQ-masked samples do not measure the duration of every masked phase. Legacy WFI handling does not expose a reliable idle percentage.

Timer expiration totals count every elapsed reload, including multiple reloads during a bulk bus advance. The controller still maintains hardware-style pending bits rather than an interrupt queue. Request counts, requests while already pending, IRQ entries and acknowledgements are different quantities: one IRQ entry can lead to handling more than one source, and an acknowledgement is not proof of a completed handler. The probe observes an IRQ entry when the CPU's existing entry condition is satisfied and records the controller's selected source at that point.

MMIO counts exclude host inspection. An interval-boundary value is not a history of all returned values; constant boundary values alone cannot prove constant guest reads. Direct NAND byte streaming retains the preceding decoder-profile limitation.

Jiffies discovery scans the loaded kernel's first 2 MiB for the `jiffies` and `jiffies_64` strings and matching value/name pairs. It accepts a unique shared address, then verifies the current translation table's direct SDRAM section mapping before each read. Inspection uses physical SDRAM without changing guest privileges, TLB state or fault registers. Unsupported or unavailable mappings leave the value blank. This diagnostic targets the observed cartridge kernel layout; it is not a general kernel symbol loader. Unsigned subtraction preserves a 32-bit jiffies wrap.

## Reproduction

```powershell
make test
make build/scene_work_probe.exe
build/scene_work_probe.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  9600000000 4 build/wade-time-probe initial.nvram tests/wade_intro_ratio4.txt
```

The tick budget covers 480 modeled seconds. `initial.nvram` is an independent 2,048-byte erased device-state image. The output directory must not exist. The headless probe executes without a wall-clock limiter, produces PCM sample counts without Windows audio output, and retains frame and SDRAM captures locally. Profiling and file writes add host work; its elapsed duration is not an isolated throughput benchmark or a sound-quality test.

```powershell
python tools/summarize_scene_timing.py build/wade-time-probe --start 430 --end 450
```

## Wade Measurements

The completed probe executes 38.4 billion step calls over 480 modeled seconds with the existing full-introduction input script and no subsequent fight input. The diagnostic executable is an O3 unity build with SHA-256 `b3d662a24375557bb7e14ab6a3165fbeeca9e1d8405f8726f0c4d2f26cdd6da9`. Cartridge SHA-256 is `2d100b17fe7da2bc316f6fc5a7814aafde4e08749862a3a5d27e5b639634e19a`; script SHA-256 is `7bd5f7030297fa0eb0895204f1204af9b40ef9822d518beaab3c01514d29d995`; initial EEPROM SHA-256 is `d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`. This probe is separate from the installed Windows executable.

The discovered jiffies export is `0xc017f1cc`. Every measured one-second delta from modeled second 4 through 480 is 200, including the unsigned wrap. The [per-second records](validation/2026-10-10_wade_guest_time.csv) retain all three time references. There are 147 requests while Timer 4's source is already pending during early startup; that cumulative count stops increasing before the measured gameplay intervals. Startup coalescence is not evidence of continued gameplay tick loss.

The inspected active-fight interval is modeled seconds **430 through 450**. The displayed round counter reads 89, 87 and 84 at 430, 440 and 450. By 460/470 it reads 82 during the knockout transition; those later images must not be treated as a continuously advancing active-fight counter. The original-console counter cadence remains unknown.

| Quantity over 20 modeled seconds | Observation |
| --- | ---: |
| CPU step calls | 1,600,000,000 |
| Jiffies increase | 4,000; every interval advances by 200 |
| Timer 4 expirations / requests | 4,000 / 4,000 |
| Further requests while Timer 4 source already pending | 0 |
| IRQ entries observed with Timer 4 selected | 3,993 |
| Timer 4 source / selected acknowledgements | 4,000 / 4,000 |
| IRQ entries observed with LCD selected | 0 |
| Guest LCD-register reads / writes | 0 / 0 |
| Userspace ioctl calls | 0 |
| Sampled framebuffer memory changes | 579; not complete frames |
| Host duration of the instrumented headless interval | 27.700 seconds |

[The interval summary](validation/2026-10-10_wade_active_fight.json) and [guest MMIO records](validation/2026-10-10_wade_fight_mmio.csv) retain the counters. The difference between IRQ-entry and acknowledgement totals must not be labeled seven lost ticks: jiffies and both acknowledgement counts advance by the expected 4,000. Selected-source observation at CPU exception entry is not equivalent to counting completed Timer 4 handlers.

Across the complete run, the [ioctl command summary](validation/2026-10-10_wade_ioctl_commands.csv) contains framebuffer information/mode requests but no command `0x40044620`, the usual `FBIO_WAITFORVSYNC` encoding defined as `_IOW('F', 0x20, __u32)` in the [later Linux framebuffer interface](https://github.com/torvalds/linux/blob/v6.1/include/uapi/linux/fb.h). There are no ioctl calls during the measured active fight. There is therefore no observed evidence of that wait path in this replay. This does not identify every private vendor ioctl or establish the behavior of all cartridges.

[PC-page samples](validation/2026-10-10_wade_fight_pages.csv) assign 48.34% to `0x40115000`, 22.18% to `0x40117000` and 15.53% to `0x40118000`, in the loaded SDL mapping with TTB `0x30fa4000`. The most frequent individual PC accounts for approximately 0.40% of interval samples; [the per-second top-PC records](validation/2026-10-10_wade_fight_top_pcs.csv) retain the distribution. These observations favor inspecting the SDL execution workload over an unobserved LCD MMIO busy-wait, but do not by themselves identify a function, prove correct ARM execution timing or exclude waits on ordinary RAM.

All nine sampled framebuffer images at modeled seconds 50 through 450 match the preceding sound-enabled recorded run at the same instruction boundaries. [Frame comparison hashes](validation/2026-10-10_wade_frame_comparison.csv) validate the reached images; they are not a complete CPU/SDRAM equivalence comparison. The ROM-free regression suite passes, including bulk timer expiration accounting, pending-bit coalescence, separate acknowledgements, OABI/EABI syscall observation and verified jiffies mapping. The current run reports 265 `PASS` lines and no failures.

These measurements do not support ongoing lost Timer 4 ticks or a framebuffer ioctl/LCD-register VSync wait as explanations for the slow Wade fight in this replay. The next discriminating experiment is additional guest CPU work per modeled second, with scene alignment and hardware calibration retained. Guest LCD status/interrupt emulation remains a separate model gap.

## Remaining Discriminating Experiment

A ratio sweep should retain modeled input times by scaling instruction-indexed events relative to ratio 4. Compare active-fight counter changes, guest time and drawing work at ratios 4, 8 and 16. Fixed modeled timestamps alone may select different scenes or an already completed fight at higher ratios; inspect the reached scenes before comparing counters. No original-console counter rate or game frame rate has yet been established.
