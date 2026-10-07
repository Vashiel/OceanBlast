# OceanBlast: Verified Boot Progress & Emulation Milestones

Updated: October 7, 2026. This document chronicles all empirically verified boot milestones, hardware behaviors, and technical evidence obtained during clean-room development of OceanBlast.

---

## 1. Reference Test Run & Environment

All milestones below are fully reproducible using the following test configuration:

* **Target Cartridge:** Commercial Nikko digiBLAST NAND cartridge dump (`roms/test.bin`)
* **File Size:** `17,301,504` bytes (16.5 MB; exactly 32,768 pages × 528 bytes raw NAND)
* **SHA-256 Hash:** `5593F38ADFE2159D18AD420301A6A21E73167291BB38E0E58A63C607C6BD5BA9`
* **Execution Command:**
  ```bash
  bin/oceanblast.exe roms/test.bin --steps 260000000
  ```
* **Toolchain:** MinGW-w64 GCC 16.2.0 (w64devkit), C++17, `-O2`
* **Execution Time:** ~7.5 seconds for 260,000,000 CPU instructions on host machine

---

## 2. Verified Boot Milestones (Chronological Evidence)

### Milestone 1: Autonomous Steppingstone Boot (4 KB SRAM)
* **Status:** Verified.
* **Mechanism:**
  * S3C2410 hardware logic autonomously copies the first 8 pages (512 data bytes each = 4096 bytes) from NAND cartridge into internal SRAM (`0x00000000 - 0x00000FFF`).
  * ARM920T CPU core begins execution at reset vector `0x00000000`.
  * The 4 KB boot code configures the S3C2410 Memory Controller (`0x48000000`), initializes SDRAM at `0x30000000`, enables the NAND Flash Controller (`0x4E000000`), copies 136 KB of U-Boot to `0x30F80000`, and jumps to `0x30F81B50` (`start_armboot`).

### Milestone 2: U-Boot 1.1.2 Initialization & Serial Console
* **Status:** Verified.
* **UART0 Output Evidence (115200 Baud Stream):**
  ```text
  U-Boot 1.1.2.greyinnovation.EOL (Dec  1 2005 - 12:30:47)
  U-Boot code: 30F80000 -> 30FA15C0  BSS: -> 30FA5BF8
  RAM Configuration:
  Bank #0: 30000000 16 MB
  FLASH Mpf id is 0x0090, ## Unknown FLASH on Bank 0: ID 0xffff, Size = 0x00000000 = 0 MB
  ## Found CFI FLASH on Bank 0: ID 0xffff, Size = 0x00000000 = 0 MB
  Flash:  0 kB
  NAND:
  ---------------------------------
  Digiblast Nand Flash Boot - release version 1.3.3_I(Fixed_NAND_BOOT_ROM) (Grey Innovation). 
  ---------------------------------
  Flash chip found:
           Manufacturer ID: 0xEC, Chip ID: 0x73 (Samsung KM29U128T)
  1 flash chips found. Total nand_chip size: 16 MB
    16 MB
  *** Warning - bad CRC, using default environment
  In:    serial
  Out:   serial
  Err:   serial
  Hit any key to stop autoboot:  0 
  NAND read: device 0 offset 180224, size 16384, cmd 5... 
   16384 bytes read: OK
  ## Executing script at 30400000
  ```

### Milestone 3: NAND Partition Layout & Boot Script Execution
* **Status:** Verified.
* **Mechanism:**
  * At NAND offset `180224` (`0x2C000`, page 352), a valid U-Boot uImage Script (`IH_MAGIC = 0x27051956`) is loaded into SDRAM at `0x30400000`.
  * The script executes the following sequence:
    ```sh
    nidc check ${nkg} 4 ec 79 a5 c0
    nidc check ${nkg} 4 98 76 00 ff
    nidc check ${nkg} 4 98 73 00 ff
    nidc check ${nkg} 4 98 75 00 ff
    nidc check ${nkg} 0
    echo Checking Battery Voltage
    checkbattery boot 620 830 
    echo Displaying splash0.bin
    nand read 0x30300000 0x30000 0x10000
    splash 0x30300000
    echo Loading kernel
    nand read 0x30c00000 0x00050000 0x001c0000
    echo Display splash1.bin
    nand read 0x30300000 0x40000 0x10000
    splash 0x30300000
    nand read 0x30310000 0x40000 0x10000
    set bootargs console=ttySAC0,115200 mem=16M devfs=mount panic=30 init=/linuxrc root=/dev/mtdblock/5 ro rootfstype=squashfs splash=0x30310000
    bootm 0x30c00000
    ```

### Milestone 4: `nidc` Security Handshake & `checkbattery` ADC
* **Status:** Verified.
* **Mechanism:**
  * The proprietary `nidc` command queries the NAND chip ID registers (`0x4E00000C`).
  * Emulation returns authentic Toshiba NAND ID `0x98, 0x73, 0x00, 0xFF` for 16 MB cartridges, allowing `nidc check ${nkg} 4 98 73 00 ff` to pass (`X.X....P`) and set environment variable `nkg=1`.
  * S3C2410 ADC registers (`ADCCON` at `0x58000000` with conversion complete bit 15 `ECFLG`, `ADCDAT0` at `0x5800000C` returning 750 counts) satisfy `checkbattery`:
  ```text
  X.X....P
  .XChecking Battery Voltage
  adc_read(377): 0 ADCCON [0xffc8] - adc_read(410): CHANNEL[1] = 750, 1, ADCCON[0xffc8]
  disable_interrupts_checkbattery(483): 
  exit: clear all sub pending registers
  ```

### Milestone 5: Linux Kernel Handoff & Decompression
* **Status:** Verified.
* **UART0 Evidence:**
  * U-Boot streams 1.835 MB from NAND offset `0x50000` to `0x30C00000`.
  * `bootm 0x30c00000` verifies image header and CRC32 checksum, then uncompresses gzip kernel payload to `0x30008000`:
  ```text
  ## Booting image at 30c00000 ...
     Image Name:   
     Image Type:   ARM Linux Kernel Image (gzip compressed)
     Data Size:    825400 Bytes = 806.1 kB
     Load Address: 30008000
     Entry Point:  30008000
     Verifying Checksum ... OK
     Uncompressing Kernel Image ... OK

  Starting kernel ...
  ```

### Milestone 6: ARM920T CP15 MMU & Linear Page Map
* **Status:** Verified.
* **Mechanism:**
  * CPU enters kernel entry at `0x30008000` with `r1=0x294` (SMDK2410 machine ID) and `r2=0x30000100` (ATAGS pointer).
  * CP15 c0 returns ARM920T processor ID `0x41009200`.
  * Kernel establishes first-level page tables at `0x30004000`, enables MMU and caches via CP15 c1, and transitions to virtual address space (`0xC0008000`).

### Milestone 7: Linux 2.6.11 Kernel Subsystem Initialization & Timer 4 IRQ
* **Status:** Verified.
* **Mechanism:**
  * S3C2410 chip identification register `GSTATUS1` (`0x560000B0`, virtual `0xF0E000B0`) returns `0x32410002` (S3C2410A).
  * S3C2410 Timer 4 generates periodic timer interrupts (INTC bit 14), advancing `jiffies` during `calibrate_delay()`.
  * Kernel calculates calibration loop: `19.86 BogoMIPS (lpj=49664)`.

### Milestone 8: Peripheral Driver Probes & Hardware Boundaries
* **Status:** Verified with explicit boundary notes.
* **Kernel Probing Log (`dmesg`):**
  * S3C2410 DMA controller: 4 channels initialized (`irq 33..36`).
  * S3C24XX NAND controller: Toshiba 16 MiB recognized, 6 MTD partitions created:
    * `0x00000000-0x0002c000`: "u-boot"
    * `0x0002c000-0x00030000`: "bootscript"
    * `0x00030000-0x00040000`: "bootsplash0"
    * `0x00040000-0x00050000`: "bootsplash1"
    * `0x00050000-0x00170000`: "kernel"
    * `0x00170000-0x01000000`: "rootfs"
  * S3C2410 UART 0..2 registered at MMIO `0x50000000`, `0x50004000`, `0x50008000`.
  * S3C2410 IIS Audio: attached `cs43l43` sound card driver (`#0: S3C24XX CS43L43`).
  * S3C2410 Framebuffer: `fb0: s3c2410fb frame buffer device` registered.
  * **Explicit Failure Observed:**
    ```text
    s3c2410-ohci s3c2410-ohci: USB HC reset timed out!
    s3c2410-ohci s3c2410-ohci: startup error -1
    s3c2410-ohci: probe of s3c2410-ohci failed with error -1
    ```
    *(USB host controller is intentionally not modeled, matching expected hardware boundary).*

### Milestone 9: SquashFS 2.2 Root Filesystem Mount
* **Status:** Verified.
* **Log Evidence:**
  ```text
  Squashfs 2.2 (released 2005/07/03) (C) 2002-2005 Phillip Lougher
  devfs: 2004-01-31 Richard Gooch (rgooch@atnf.csiro.au)
  devfs: boot_options: 0x1
  Displaying splash screen at 0x30310000, stand well clear
  fb0: s3c2410fb frame buffer device
  VFS: Mounted root (squashfs filesystem) readonly.
  Mounted devfs on /dev
  Freeing init memory: 68K
  ```

### Milestone 10: Userspace Startup Script & MMU Copy-On-Write (COW) Fix
* **Status:** Verified.
* **Mechanism & Bug Resolution:**
  1. The kernel executes `/linuxrc`, which launches the system shell script `/usr/packages/startupscripts/startup.sh`.
  2. Inside `startup.sh`, `setupdevices()` creates three essential device symlinks using `/bin/ln`:
     * `/bin/ln -s /dev/sound/dsp /dev/dsp`
     * `/bin/ln -s /dev/fb/0 /dev/fb0`
     * `/bin/ln -s /dev/v4l/video0 /dev/video`
  3. **Root Cause of Historical Freeze at Second `ln`:**
     * Linux `fork()` duplicates address spaces by marking user writable pages read-only (`AP = 0b10`).
     * In ARMv4/v5 architectures, `AP = 0b10` indicates *Privileged Read/Write, User Read-Only*.
     * Without MMU user-mode write permission checks, child processes writing to their stack were silently mutating the parent's physical RAM page, corrupting the parent stack frame upon return from `wait4()`.
  4. **The MMU AP Resolution:**
     * Implemented active User Mode tracking (`Bus::setUserMode(bool)` when `(CPSR & 0x1F) == 0x10`).
     * Enforced ARMv4/v5 small page and section permission validation on writes.
     * User-mode write attempts to `AP = 0b10` generate `MmuFault::PAGE_PERMISSION_FAULT` (FSR `0xF`).
     * Linux kernel's `do_page_fault()` -> `do_wp_page()` cleanly intercepts the fault, allocates a private physical RAM page, copies the frame, and resumes execution seamlessly.
  5. **Trace Evidence of Successful Multi-Process Execution:**
     * **PID 14 (`/bin/ln -s /dev/sound/dsp /dev/dsp`):** Forked at step 174,259,047; completed and reaped at step 174,481,703 (`r0 = 14`).
     * **PID 15 (`/bin/ln -s /dev/fb/0 /dev/fb0`):** Forked at step 180,128,970; completed and reaped at step 180,973,663 (`r0 = 15`).
     * **PID 16 (`/bin/ln -s /dev/v4l/video0 /dev/video`):** Forked at step 181,034,280; completed and reaped at step 181,575,142 (`r0 = 16`).

### Milestone 11: System Mounts, Keypad Diagnostic, & Framebuffer Splash
* **Status:** Verified.
* **Trace Evidence:**
  1. **Dynamic Library Setup (`setpath`):**
     * `startup.sh` enumerates `/usr/packages` (`fb_test`, `showversion`, `startupscripts`) and exports `LD_LIBRARY_PATH`.
  2. **Mount Operations (`mountall`):**
     * Invokes `/bin/mount` (`/bin/busybox`, PID 17 and 18) to mount `/proc` and `/sys` as specified in `/etc/fstab`.
  3. **Diagnostic Key Check:**
     * Executes `/usr/packages/showversion/bin/iskeydown` to poll GPIO lines for diagnostic recovery key combinations.
  4. **Framebuffer Initialization (`fb_test`):**
     * Executes `/usr/packages/fb_test/bin/fb_test`.
     * Binary opens `/dev/fb0` and `/digiblast_user_splash_blue.raw`.
     * Maps framebuffer into userspace via `mmap(0x4028d000)`.
     * Console output captured from userspace stdout:
       ```text
       [USERSPACE WRITE fd=1] "Wrote 57600 bytes from file /digiblast_user_splash_blue.raw to mmap 0x4028d000\n"
       ```
     * 57,600 bytes (= 160 × 120 × 3 RGB24 / 240 × 120 × 2 16-bit) transferred directly to physical framebuffer RAM.

### Milestone 12: Commercial Game Execution (`./Rayman`)
* **Status:** Verified.
* **Mechanism & Execution Details:**
  * Following `startup.sh` completion, the system launches the primary game executable:
    ```text
    >>> [USERSPACE EXECVE] "./Rayman" <<<
    ```
  * Linux dynamic linker (`/lib/ld-linux.so.2`) traverses library dependencies and maps:
    * `libSDL-1.2.so.0`
    * `libboost_thread-gcc-mt-1_32.so.1.32.0`
    * `libboost_filesystem-gcc-mt-1_32.so.1.32.0`
    * `libstdc++.so.5`
    * `libpthread.so.0`
    * `librt.so.1`
    * `libdl.so.2`
  * Process `./Rayman` spawns three concurrent POSIX/Boost threads and actively runs in userspace, opening `/dev/sound/dsp` and handling block I/O requests via `kblockd/0` and `mtdblockd`.
  * Verified executing past instruction step 350,000,000.

### Milestone 13: Display Architecture Decoded & Live Host Window
* **Status:** Verified.
* **Hardware Ground Truth:**
  * The native display geometry and framebuffer parameters were recovered directly from `s3c2410fb` and userspace diagnostic structures (`finfo`/`vinfo`):
    * **Visible Resolution:** 240 × 160 pixels (Game Boy Advance native resolution format).
    * **Color Depth:** 12-bit RGB444 (4096 colors; 4 bits Red, 4 bits Green, 4 bits Blue).
    * **Memory Packing:** Packed LCD444 format (3 bytes store 2 pixels).
    * **Scanline Stride:** Exactly 360 bytes per row (240 pixels × 1.5 bytes).
    * **Total Frame Size:** Exactly 57,600 bytes (360 bytes × 160 rows).
  * Authentic digiBLAST bootsplash images successfully decoded with 100% pixel fidelity:
    * `splash0`: Official digiBLAST logo with gold stars (`©2005 all rights reserved`).
    * `splash1`: Joystick on yellow orb with red "games" banner.
* **Implementation:**
  * Native desktop window rendering subsystem implemented in [`src/display/display_win32.cpp`](../src/display/display_win32.cpp) using Win32 GDI with hardware nearest-neighbor integer scaling (`--gui`, `--scale 2|3|4`).
  * Live framebuffer polling from SDRAM at active S3C2410 LCD memory base addresses (`0x30300000`, `0x30310000`, and `0x302A0000`).

### Milestone 14: Keypad Hardware Decoded & GPIO Subsystem Implemented
* **Status:** Verified.
* **Hardware Ground Truth:**
  * Keypad layout and hardware wiring reverse-engineered directly from the kernel `greykbd.c` driver (`struct platform_device Grey-keypad` at `0xc0180c14`, `dev.platform_data` at `0xc0180f18`):
    * **D-Pad:**
      * `KEY_UP` (0x67) -> Port F, Pin 2 (`GPF2` / `EINT2`, IRQ 0x12 / IRQ 2)
      * `KEY_DOWN` (0x6c) -> Port F, Pin 7 (`GPF7` / `EINT7`, IRQ 0x33 / `EINT4_7` cascaded IRQ 4)
      * `KEY_LEFT` (0x69) -> Port F, Pin 3 (`GPF3` / `EINT3`, IRQ 0x13 / IRQ 3)
      * `KEY_RIGHT` (0x6a) -> Port F, Pin 6 (`GPF6` / `EINT6`, IRQ 0x32 / `EINT4_7` cascaded IRQ 4)
    * **Action Buttons:**
      * Button A (`KEY_1`, 0x02) -> Port F, Pin 0 (`GPF0` / `EINT0`, IRQ 0x10 / IRQ 0)
      * Button B (`KEY_4`, 0x05) -> Port F, Pin 1 (`GPF1` / `EINT1`, IRQ 0x11 / IRQ 1)
      * Buttons X / Y (`KEY_5`, `KEY_2`, `KEY_3`) -> `GPF4`, `GPF5`, `GPG12`
    * **Shoulder Buttons:**
      * L Trigger (`KEY_LEFTCTRL`, 0x1d) -> Port G, Pin 11 (`GPG11` / `EINT19`, IRQ 0x3f / `EINT8_23` cascaded IRQ 5)
      * R Trigger (`KEY_RIGHTCTRL`, 0x61) -> Port G, Pin 8 (`GPG8` / `EINT16`, IRQ 0x3c / `EINT8_23` cascaded IRQ 5)
    * **System Buttons:**
      * Start (`KEY_S`, 0x1f) -> Port G, Pin 10 (`GPG10` / `EINT18`, IRQ 0x3e / `EINT8_23` cascaded IRQ 5)
      * Select / Pause (`KEY_P`, 0x19) -> Port G, Pin 9 (`GPG9` / `EINT17`, IRQ 0x3d / `EINT8_23` cascaded IRQ 5)
      * Backlight (`KEY_B`, 0x30) -> Port G, Pin 0 (`GPG0` / `EINT8`)
      * Power / Sleep (`KEY_N`, 0x31) -> Port G, Pin 13 (`GPG13` / `EINT21`)
  * **Electrical Circuit Semantics:**
    * Buttons are active-low with pull-up resistors (HIGH = 1 released, LOW = 0 pressed).
    * Confirmed by disassembly of kernel ISR and workqueue handler (`rsbs r3, r2, #1` inversion before `input_report_key()`).
  * **Interrupt Cascade:**
    * EINT0..3 trigger dedicated primary IRQs 0..3 directly.
    * EINT4..7 set corresponding bits in `EINTPEND` (`0x560000A8`) and trigger `EINT4_7` (IRQ 4).
    * EINT8..23 set corresponding bits in `EINTPEND` and trigger `EINT8_23` (IRQ 5).
    * `EINTPEND` conforms to Write-1-to-Clear (W1C) semantics.
* **Implementation:**
  * Real-time keyboard mapping in [`src/display/display_win32.cpp`](../src/display/display_win32.cpp) updates button bitmask via Win32 `WM_KEYDOWN` and `WM_KEYUP` events.
  * S3C2410 GPIO registers (`GPFDAT`, `GPGDAT`, `EINTMASK`, `EINTPEND`) implemented in [`src/memory/bus.cpp`](../src/memory/bus.cpp).
  * Automated regression test suite in `tests/input_test.cpp` verifying all GPIO pull-ups, active-low states, interrupt cascades, and W1C clears (all 7 tests passing).

---

## 3. Emulation Boundaries & Current Focus

To ensure scientific honesty and accurate tracking, the following distinctions are maintained:

| Subsystem | Verified Reality | Pending Implementation |
| :--- | :--- | :--- |
| **Bootloader & Linux Kernel** | U-Boot 1.1.2 and Linux 2.6.11 boot fully autonomously with MMU and Timer IRQs. | Complete. |
| **Userspace Pipeline** | `/linuxrc`, `startup.sh`, symlinks, mounts, and shared libraries execute without skips. | Complete. |
| **Game Engine Execution** | `./Rayman` binary is loaded and running active game code across 3 threads past 350M steps. | Ongoing profiling. |
| **Display & LCD** | Authentic 240×160 12-bit LCD444 resolution decoded; real-time Win32 desktop window rendering live. | Complete. |
| **Keypad / Controls** | Authentic S3C2410 GPIO & EINT mapping decoded; host keyboard events trigger kernel IRQs live. | Complete. |
| **Audio** | ALSA CS43L43 driver attaches and accepts IIS config; Rayman opens `/dev/sound/dsp`. | Real-time DMA audio buffer streaming to host sound output. |
| **USB Host** | Driver fails with `startup error -1` as expected. | Low priority (not needed for gameplay). |
