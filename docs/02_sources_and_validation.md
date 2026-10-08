# OceanBlast: Technical Sources & Verification Framework

Updated: October 7, 2026. This document establishes the technical reference foundation, external documentation, memory map corrections, and verification methodology used in OceanBlast.

---

## 1. Primary Technical Sources

Register-level clock and IIS corrections are specified in [Register clocks and IIS playback control](23_register_clocks_and_iis_pause.md), with direct links to the relevant Samsung manual sections, ARM920T instruction timing, and validation boundaries.

* **Samsung S3C2410A User's Manual (Revision 1.0, March 2004)**
  * URL: https://bitsavers.org/components/samsung/S3C204x/S3C2410/21-S3-C2410A-032004_S3C2410A_Users_Manual_1.0_200403.pdf
  * Primary architectural reference for the Samsung S3C2410 application processor (the base architecture of the OCEAN-L-20 SoC), ARM920T core, MMU/CP15, bus memory controller, Steppingstone 4 KB boot SRAM, NAND Flash controller, interrupt controller, timers, and peripheral interfaces.
* **MAME digiBLAST Driver Implementation**
  * Reference: `src/mame/skeleton/digiblast.cpp` (https://github.com/mamedev/mame/blob/master/src/mame/skeleton/digiblast.cpp)
  * Skeleton driver documenting 200 MHz ARM9 core clock, S3C2410 12 MHz input crystal, and 32 MB SDRAM address space. Notes system as `MACHINE_NOT_WORKING` and `MACHINE_NO_SOUND`.
* **MAME S3C2410 Device Model**
  * Reference: `src/devices/machine/s3c2410.cpp` (https://github.com/mamedev/mame/blob/master/src/devices/machine/s3c2410.cpp)
  * Comparative reference for peripheral registers and MMIO offsets.
* **MAME Cartridge Software List (`digiblast_cart.xml`)**
  * Reference: `hash/digiblast_cart.xml` (https://github.com/mamedev/mame/blob/master/hash/digiblast_cart.xml)
  * Reference for known retail dumps, cartridge geometries, and SHA-1/CRC verification hashes.

---

## 2. Memory Map Verifications & Corrections

Initial preliminary documentation had minor offset mismatches which have been aligned directly with the Samsung hardware manual and the emulator source (`src/memory/bus.h`):

| Peripheral Subsystem | Physical Base Address | Preliminary Documented Error |
| :--- | :--- | :--- |
| **IIS Audio Interface** | `0x55000000` | Misidentified as `0x5B000000` |
| **Real-Time Clock (RTC)** | `0x57000000` | Misidentified as `0x58000000` |
| **ADC & Touch Screen** | `0x58000000` | Misidentified as `0x59000000` |
| **Watchdog Timer (WDT)** | `0x53000000` | Conflated with PWM timers at `0x51000000` |

SPI controller resides at `0x59000000`; SD/MMC interface resides at `0x5A000000`.

---

## 3. Emulation Baseline & Ground Truth

1. **Autonomous Steppingstone Boot (4 KB SRAM):**
   * The hardware autonomous copy reads the first 8 pages (512 data bytes each = 4096 bytes) into internal Steppingstone SRAM (`0x00000000 - 0x00000FFF`).
2. **NAND Cartridge Geometry & OOB Handling:**
   * Commercial cartridges use 528-byte pages (512 data bytes + 16 spare/OOB bytes).
   * For dumps preserving authentic OOB data, the raw OOB bytes must be passed through directly.
   * For dumps with blanked OOB (`0xFF`), standard Linux MTD 256-byte 1-bit Hamming ECC is dynamically calculated to satisfy U-Boot's verification checks.
3. **Hardware Device Modeling vs. Driver Stubs:**
   * Successful driver initialization in the Linux kernel log (`dmesg`) indicates that driver probes succeeded, not that complete hardware emulation is finished.
   * For example, `s3c2410-ohci` (USB host) logs `startup error -1` and fails probing, while IIS audio and I2C devices accept initial configuration registers without live audio synthesis or bus arbitration.
   * Distinctions must be strictly maintained between:
     - U-Boot static BMP splash extraction from SDRAM,
     - Framebuffer writes (`/dev/fb0`) by test binaries or game engines,
     - Live display presentation in the emulator window,
     - Interactive gameplay with keypad inputs and sound.

---

## 4. Verification Standards

All progress claims in OceanBlast must be backed by reproducible empirical execution:
1. Exact Cartridge SHA-256 hash, byte size, and verified MTD partition layout.
2. Exact invocation command and step execution boundary.
3. Matching UART console output, process execution events, and exception trace excerpts.
4. Clean separation between verified milestones, inferred behaviors, and pending features.
