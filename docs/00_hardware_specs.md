# Nikko digiBLAST (2005) - Hardware Specifications

The **digiBLAST** is a 32-bit handheld multimedia console released in Europe in late 2005 by **Nikko Entertainment B.V.** and **Giochi Preziosi**. It was designed by Australian engineering firm **Grey Innovation**.

---

## 1. System Specifications

| Component | Specification |
| :--- | :--- |
| **Manufacturer** | Nikko Entertainment B.V. & Giochi Preziosi |
| **Designer** | Grey Innovation (Australia) |
| **Release Date** | October 2005 (Europe: UK, Italy, Germany, France, Netherlands) |
| **Main SoC** | Samsung **OCEAN-L-20** (Application processor based on Samsung S3C2410 family) |
| **CPU Core** | **ARM920T** (ARMv4T architecture, 32-bit ARM + 16-bit Thumb) |
| **CPU Clock** | ~133 MHz - 200 MHz |
| **System RAM** | **32 MB SDRAM** (mapped at `0x30000000 - 0x31FFFFFF`) |
| **Display** | 2.7-inch Color TFT LCD (4096 colors / 16-bit color mode) |
| **Display Resolution** | 160x120 / 320x240 pixels |
| **Audio** | Integrated Stereo DAC, IIS / PWM sound output, built-in speaker, 3.5mm headphone jack |
| **Cartridge Media** | Proprietary NAND-Flash memory cartridges (8 MB to 64 MB NAND chips) |
| **Software Platform** | Embedded Linux kernel (`vmlinux`) loaded from cartridge NAND |
| **Add-On Modules** | 1.3 MP Digital Camera module, 256 MB MP3 Music Player module, TV-Out cable |
| **Power** | 4x AA Batteries or 6V DC AC Adapter |

---

## 2. Processor Architecture: ARM920T

* **Core:** 32-bit Harvard architecture with separate instruction and data caches (16 KB I-Cache, 16 KB D-Cache).
* **Instruction Sets:**
  * 32-bit ARM instruction set
  * 16-bit high-density Thumb instruction set
* **Registers:** 31 32-bit registers (16 visible at any time: `r0`-`r15`), CPSR, 5 SPSRs.
* **Five-Stage Pipeline:** Fetch, Decode, Execute, Buffer/Data, Writeback.

---

## 3. Cartridge NAND-Flash Subsystem

Unlike traditional ROM consoles (such as the Game Boy Advance or SNES), the digiBLAST uses **NAND Flash memory** inside its cartridges:
* Cartridge signals are interfaced through the Samsung S3C/OCEAN NAND flash controller.
* Standard NAND commands (`0x00` Read, `0x90` Read ID, `0x30` Read Confirm).
* The cartridge contains a master boot record (MBR) and a compressed Linux kernel/filesystem image that boots into RAM on startup.
