# OceanBlast

**OceanBlast** is an open-source, independent research and emulation project targeting the **Nikko digiBLAST** (2005), a European multimedia handheld console powered by the Samsung OCEAN-L-20 System-on-a-Chip (Samsung S3C2410 architecture, ARM920T CPU core).

The goal of this project is digital preservation, architectural documentation, and software interoperability for an obscure and historically undocumented platform.

---

## 🚀 Quick Start & Windows Launcher

Double-click `bin/oceanblast.exe` without arguments to launch the graphical interface. Click **Browse ROM…**, choose display scaling (2×, 3×, 4×), optionally enable sound, and click **Start Game**. The launcher includes session controls, optional FPS/debug logging, and built-in keyboard help. Command-line invocation remains fully supported. See [Windows launcher documentation](docs/06_windows_launcher.md).

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
| **Audio** | S3C2410 IIS bus (`0x55000000`) with DMA playback (nominal 22.05 kHz stereo, register-derived source rates and dynamic resampling) |
| **Debug Console** | S3C2410 UART0 (`0x50000000`) streamed via 115200 baud serial console |

---

## 📈 Emulation Progress

### Current compatibility status (2026-10-09)

[Programmed LCD scanout height](docs/31_programmed_scanout_height.md) includes video rows previously omitted by the fixed 160-row display path.

[RealVideo frame-delivery tests](docs/30_realvideo_frame_delivery.md) show Sonic X and Italian/Spanish Winx video imagery at a higher diagnostic CPU allowance. Both decode colored frames while remaining dark at the standard allowance. Hardware timing, continuous audio and synchronized playback remain unresolved.

[Gameplay profiling and optimized CPU execution](docs/29_gameplay_work_and_execution_cost.md) identify substantial mixer and software drawing costs. A validated profile-guided build improves controlled instruction throughput by 21–27% across Chefs, Wade and DigiQUAD and reduces observed Chefs audio starvation at 4x. Game speed and sound acceptance remain incomplete.

[Register-derived clocks and IIS playback control](docs/23_register_clocks_and_iis_pause.md) make Timer 4 and audio follow programmed clocks, preserve exact integer audio rates, pause IIS DMA when the transmit path is disabled, and advance peripheral time on IRQ entry. These corrections do not establish hardware-equivalent CPU speed or complete cartridge compatibility.

The [automatic timing experiment and Superstar Chefs load analysis](docs/24_automatic_timing_and_chefs_load.md) distinguish slow animation at a sustained 40 MIPS from audio queue starvation. Register-clock CPU execution is available as an experimental mode, with modeled speed diagnostics. It remains incomplete and is not enabled by default.

[Bounded execution batches](docs/26_execution_batches_and_mmio_diagnostics.md) reduce host bookkeeping while preserving tested CPU, memory, interrupt and DMA states. [Windows host pacing](docs/27_windows_host_pacing.md) uses a high-resolution waitable timer to reduce short-deadline jitter. These changes do not establish complete game or synchronized media compatibility.

[Consumed-sample DMA capture](docs/20_pcm_capture_and_dma_sample_lifetime.md) corrects source RAM being reread after a game has reused it. A controlled DigiQUAD comparison reduces large changes at audio callback boundaries from 127 to 13; listening confirms a significant improvement, with residual defects. The [manual cartridge reports](docs/18_manual_cartridge_validation.md) preserve the remaining speed, input and audio issues. Normal audio and synchronized gameplay are not yet established.

The [EEPROM and player-startup investigation](docs/16_i2c_eeprom_and_player_startup.md) identifies missing I2C EEPROM transactions as a startup blocker. Crazy Jack reaches its title, level selection and an interactive gameplay scene. [Automatic display selection](docs/25_automatic_display_selection.md) recognizes its checked dump and selects packed RGB444/480-byte rows only for the matching game framebuffer configuration. Boot splashes retain native packed rows. The underlying register/pixel-format disagreement remains unresolved.

Both Italian/Spanish SpongeBob and Winx combined cartridges now reach visible episode images after language and launcher selection. All nine tested video-only cartridges start their media player. Several still produce black or near-black output. The [Timer 4 correction](docs/17_timer_and_runtime_validation.md) restores a first episode image on video-only Italian/Spanish Winx; continued playback and Netherlands Winx output remain unresolved. Complete gameplay and smooth, synchronized movie playback remain open acceptance goals. The earlier [game matrix](docs/12_game_cartridge_compatibility.md) and [UART investigation](docs/15_uart_video_startup.md) preserve the preceding baselines.

The [cartridge register audit](docs/28_cartridge_register_audit.md) completes bounded runs for all eleven games and nine video cartridges without guest segmentation faults or kernel panics. Black media output remains unresolved; guest-only counters show no continued LCD register polling during the final measured video interval.

Cartridge identity and source-data limitations are documented in the [MAME checksum and ROM integrity report](docs/14_rom_integrity.md). Twenty-one of 25 checked files/archive members match the reference in size, CRC32 and SHA-1; four variants are unlisted. The matching international Winx combined dump contains independently reproducible invalid SquashFS/zlib blocks. The alternative MAME Italian/Spanish Winx set is not available locally for comparison.

The latest changes add streaming audio-rate conversion, aggregation of small audio fragments, DMA current-position registers, GUI pacing at 20 MIPS, and audio diagnostics. The short Spider-Man comparison reported zero dropped output samples after these changes; this is a boot-only measurement and does not establish synchronized gameplay audio. See the [audio timing investigation, verification and open issues](docs/11_audio_timing_followup.md) for reproduction steps and the next investigation targets. The component milestones below do not imply complete game support.

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
* [x] **Live Host Display Output:** Real-time native desktop window with dynamic S3C2410 dual color depth support (16-bit RGB565 true-color and 12-bit packed LCD444 for bootloader splashes and titles like *Pitfall*) directly from SDRAM with configurable integer scaling (`--gui`, `--scale 2|3|4`).
* [x] **Host Keypad & GPIO Input Subsystem:** Reverse-engineered hardware pin wiring from the kernel `greykbd.c` driver; host keyboard events are converted into active-low S3C2410 GPIO states (`GPFDAT`, `GPGDAT`) and trigger `EINT0..3`, `EINT4_7`, and `EINT8_23` interrupts directly to the Linux input subsystem (`/dev/input/event0`).
* [x] **Real-Time Audio Output:** Hardware modeling of S3C2410 DMA Channel 2 (`0x4B000080`) and IIS FIFO; periodic audio buffer delivery generates `INT_DMA2` (IRQ 35), driving ALSA `snd-pcm-oss` buffer replenishment and streaming live 16-bit signed stereo PCM through a Win32 `waveOut` audio backend (`--sound`, `--gui`).
* [x] **Audio Timing and Diagnostics:** Dynamic IIS rate detection, streaming linear resampling, aggregated waveOut submissions and DMA current-position registers. GUI pacing defaults to the existing 20-MIPS timing model. Per-cartridge pitch, underruns and audio/video synchronization remain under investigation; jitter-free playback is not established.

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
    src/audio/audio_win32.cpp \
    -lgdi32 -luser32 -lwinmm -lcomdlg32 \
    -o bin/oceanblast.exe
```

### Running Test Suites
OceanBlast includes ROM-free CPU/DMA regression, GPIO keypad and streaming audio resampler verification suites:
```bash
make test
```

### Running
Double-click `bin/oceanblast.exe` for the Windows GUI launcher, or run via command line:
```bash
bin/oceanblast.exe <path_to_cartridge_dump.bin> [--steps <N>] [--gui] [--scale <2|3|4>] [--sound] [--audio-rate <Hz>] [--clock-mips <N>] [--profile] [--trace]
```

Example (interactive GUI with sound):
```bash
bin/oceanblast.exe "roms/test.bin" --gui --scale 3 --sound
```

GUI execution defaults to a 20-MIPS limit; `--clock-mips 0` disables it for diagnostics. This is an instruction-based approximation, not cycle-accurate ARM920T timing. `--profile` writes `performance.csv` in the working directory with presentation rate, framebuffer-change rate, MIPS, inferred audio rate and cumulative dropped output samples. The window title shows the audio rate and dropped-sample counter too. Zero dropped samples does not rule out underruns or audio/video drift.

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

## 📄 License
This project is licensed under the **GNU General Public License v3.0** (GPLv3). See the [LICENSE](LICENSE) file for the full license text.
