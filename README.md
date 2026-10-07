# OceanBlast

**OceanBlast** is an open-source, independent research and emulation project targeting the **Nikko digiBLAST** (2005), a European multimedia handheld console powered by the Samsung OCEAN-L-20 System-on-a-Chip (Samsung S3C2410 architecture, ARM920T CPU core).

The goal of this project is digital preservation, architectural documentation, and software interoperability for an obscure and historically undocumented platform.

---

## ⚖️ Legal & Intellectual Property Notice

### 1. Independent Clean-Room Implementation
OceanBlast is developed independently as a clean-room educational and digital preservation project. The emulator code is written from the ground up using publicly available hardware documentation (e.g., the *Samsung S3C2410A User's Manual*) and technical observation of hardware protocols.

### 2. No Proprietary Assets, Firmware, or ROMs
* OceanBlast **does not contain, distribute, host, or link to** any proprietary software, firmware, BIOS binaries, copyrighted game ROMs, commercial operating system images, or cryptographic secrets.
* All testing and usage of this emulator require the user to provide their own legally acquired cartridge dumps and data for personal educational and preservation purposes.
* Any game titles, video files, or software referenced in documentation or commit histories are mentioned strictly for descriptive compatibility and testing identification.

### 3. Interoperability & Reverse Engineering
Development of OceanBlast is conducted exclusively for the purpose of research, educational analysis, digital preservation, and software interoperability in strict accordance with applicable statutory provisions:
* **European Union:** Article 6 of Directive 2009/24/EC of the European Parliament and of the Council of 23 April 2009 on the legal protection of computer programs (Decompilation for the purpose of achieving interoperability).
* **United States:** 17 U.S.C. § 1201(f) (Reverse Engineering exemption of the Digital Millennium Copyright Act for interoperability of an independently created computer program).
* **Germany:** § 69e Urheberrechtsgesetz (UrhG) (Dekompilierung zur Herstellung von Interoperabilität).

### 4. Trademark & Nominative Fair Use Notice
All product names, logos, brands, trademarks, and registered trademarks—including but not limited to **Nikko**, **digiBLAST**, **Grey Innovation**, **Samsung**, **RealPlayer**, **Ubisoft**, and individual game titles—are the property of their respective owners. 

All company, product, and service names used in this repository are for identification, historical reference, and nominative fair use purposes only. The use of these names, logos, and brands does not imply endorsement, sponsorship, or affiliation with the owners of those marks. OceanBlast is completely independent and is not affiliated with, authorized, maintained, sponsored, or endorsed by Nikko Entertainment B.V., Grey Innovation Pty Ltd, Samsung Electronics Co., Ltd., or any game publishers.

### 5. Disclaimer of Warranty & Limitation of Liability
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

---

## 🏛️ Technical Overview

The Nikko digiBLAST hardware is structured around the Samsung S3C2410A application processor:

| Component | Hardware Specification |
| :--- | :--- |
| **CPU Core** | ARM920T (ARMv4T architecture, 5-stage pipeline, MMU, CP15, separate 16KB I-Cache / 16KB D-Cache) |
| **SoC** | Samsung OCEAN-L-20 (S3C2410 derivative / rebrand) |
| **System Memory** | 32 MB SDRAM (`0x30000000 - 0x31FFFFFF`, with alias at `0x32000000`) |
| **Boot SRAM** | 4 KB internal Steppingstone SRAM (`0x00000000 - 0x00000FFF`) |
| **Cartridge Bus** | S3C2410 NAND Flash controller (`0x4E000000`) interfacing 8-bit NAND chips (Toshiba TC58 / Samsung K9 / KM29 series) |
| **NAND Format** | 528 bytes per page (512 data bytes + 16 spare/OOB bytes) |
| **Operating System** | Embedded Das U-Boot 1.1.2 bootloader loading Linux 2.4/2.6 kernel and SquashFS root filesystem |
| **Display** | 2.7" TFT LCD (S3C2410 LCD controller at `0x4D000000`, 4096 colors / 16-bit RGB) |
| **Audio** | S3C2410 IIS bus (`0x55000000`) with DMA playback |
| **Debug Console** | S3C2410 UART0 (`0x50000000`) streamed via 115200 baud serial console |

---

## 📈 Emulation Progress

* [x] **Autonomous Boot SRAM:** S3C2410 Steppingstone hardware logic autonomously parsing initial 4 KB bootloader into internal SRAM (`0x00000000`).
* [x] **ARM920T CPU Core & CP15:** 32-bit ARM instruction interpreter with condition evaluation, branch exchange (`BX`), block transfer (`LDM`/`STM`), barrel shifter, coprocessor CP15 transfers (`MRC`/`MCR`), and virtual memory address translation.
* [x] **SDRAM & Memory Controller:** Dynamic physical bus mapping with SDRAM mirroring and MMIO routing.
* [x] **NAND Flash Controller:** Hardware registers `NFCONF`, `NFCMD`, `NFADDR`, `NFDATA`, and `NFSTAT`.
* [x] **Chip ID & Security Handshake (`nidc`):** Accurate Read ID (`0x90`) responses for production Toshiba NAND flash (`0x98, 0x73` for 16MB) allowing U-Boot's proprietary security check `nidc` to validate successfully.
* [x] **OOB & ECC Architecture:** Passthrough of authentic raw OOB data for complete dumps; dynamic reconstruction of standard Linux MTD 256-byte 1-bit Hamming ECC for blank-OOB dumps.
* [x] **U-Boot 1.1.2 Execution:** Bootloader successfully verifies memory, configures RAM, identifies NAND flash, relocates to high SDRAM (`0x30F80000`), and executes interactive boot scripts.
* [x] **S3C2410 ADC Subsystem (`checkbattery`):** ADC conversion registers (`ADCCON`, `ADCDAT0`) returning proper battery counts within the boot validation window.
* [x] **Linux Kernel Handoff & Decompression:** U-Boot loads the 1.8 MB kernel image from cartridge NAND, verifies CRC, unpacks via gunzip to `0x30008000`, and passes control with ATAGS parameters.
* [x] **Linux 2.6.11 Kernel Boot:** ARM920T MMU page table traversal, virtual memory switch (`0xC0000000`), S3C2410A chip identification (`GSTATUS1 = 0x32410002`), clock management, memory zones, slab allocator, and early `dmesg` logging.
* [x] **System Timer 4 IRQ:** Timer 4 interrupt delivery advances `jiffies` during `calibrate_delay()`, completing calibration loop at 19.86 BogoMIPS.
* [x] **Peripheral Driver Probes:** S3C2410 DMA (4 channels), UART0..2, I2C, ALSA CS43L43 audio driver, and `s3c2410fb` framebuffer device initialized. *(Note: USB Host `s3c2410-ohci` fails with startup error -1 as expected).*
* [x] **SquashFS 2.2 RootFS Mount:** Kernel mounts MTD partition 5 (`/dev/mtdblock/5`) as read-only SquashFS root filesystem and initializes `devfs`.
* [x] **Userspace Pipeline & ARMv4/v5 Copy-On-Write (COW):** MMU Access Permission (AP) checking implemented, generating `PAGE_PERMISSION_FAULT` (FSR `0xF`) on user-mode stack writes to shared pages. Linux `do_wp_page()` successfully isolates child/parent stack frames.
* [x] **Multi-Process Initialization (`startup.sh`):** `/linuxrc` and `/usr/packages/startupscripts/startup.sh` successfully execute all three symlinks (`/dev/dsp`, `/dev/fb0`, `/dev/video`), set dynamic library paths (`setpath`), and mount system filesystems via `/bin/mount` (`/bin/busybox`).
* [x] **Diagnostic Keypad & Framebuffer Splash (`fb_test`):** `/usr/packages/showversion/bin/iskeydown` tests hardware button lines; `/usr/packages/fb_test/bin/fb_test` opens `/dev/fb0` and transfers 57,600 bytes of splash image data to framebuffer memory.
* [x] **Commercial Game Binary Launch:** Startup script executes game binary (`./Rayman`); Linux dynamic linker maps `libSDL-1.2.so.0`, `libboost_thread`, `libboost_filesystem`, `libstdc++.so.5`, `libpthread.so.0`, and `libdl.so.2`; game code actively scheduled past 260M steps.
* [x] **Live Host Display Output:** Real-time native desktop window rendering the authentic S3C2410 240×160 12-bit packed LCD444 framebuffer directly from SDRAM with configurable integer scaling (`--gui`, `--scale 2|3|4`).
* [x] **Host Keypad & GPIO Input Subsystem:** Reverse-engineered hardware pin wiring from the kernel `greykbd.c` driver; host keyboard events are converted into active-low S3C2410 GPIO states (`GPFDAT`, `GPGDAT`) and trigger `EINT0..3`, `EINT4_7`, and `EINT8_23` interrupts directly to the Linux input subsystem (`/dev/input/event0`).
* [ ] **Real-Time Audio Output:** Streaming DMA audio buffers from IIS controller to host sound driver.

---

## 🛠️ Building & Running

### Prerequisites
* A C++17 compatible compiler (`g++`, MinGW-w64, or MSVC)
* `make` (optional, for automated builds)

### Building from Source
Using `make`:
```bash
make
```

Or compiling directly with `g++`:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -Isrc \
    src/main.cpp \
    src/memory/bus.cpp \
    src/cpu/arm920t.cpp \
    src/cartridge/cart_parser.cpp \
    src/display/display_win32.cpp \
    -lgdi32 -luser32 \
    -o bin/oceanblast.exe
```

### Running Test Suites
OceanBlast includes ROM-free CPU regression and GPIO keypad verification suites:
```bash
make test
```

### Running
```bash
bin/oceanblast.exe <path_to_cartridge_dump.bin> [--steps <N>] [--gui] [--scale <2|3|4>] [--trace]
```

Example (interactive GUI):
```bash
bin/oceanblast.exe "roms/test.bin" --gui --scale 3
```

#### Default Keyboard Controls
| Console Button | Hardware Line | Host Keyboard Key |
| :--- | :--- | :--- |
| **D-Pad Up** | `GPF2` / `EINT2` | `Up Arrow` |
| **D-Pad Down** | `GPF7` / `EINT7` | `Down Arrow` |
| **D-Pad Left** | `GPF3` / `EINT3` | `Left Arrow` |
| **D-Pad Right** | `GPF6` / `EINT6` | `Right Arrow` |
| **Button A** | `GPF0` / `EINT0` | `Z` or `K` |
| **Button B** | `GPF1` / `EINT1` | `X` or `J` |
| **L Shoulder** | `GPG11` / `EINT19` | `A` or `Q` |
| **R Shoulder** | `GPG8` / `EINT16` | `S` or `W` |
| **Start** | `GPG10` / `EINT18` | `Enter` |
| **Select / Pause** | `GPG9` / `EINT17` | `Space` |
| **Exit Window** | — | `Escape` |

---

## 📄 License
This project is licensed under the **GNU General Public License v3.0** (GPLv3). See the [LICENSE](LICENSE) file for the full license text.
