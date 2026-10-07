# Nikko digiBLAST / Samsung OCEAN-L-20 (S3C2410) - Memory Map

---

## 1. Physical Memory Banks

| Address Range | Size | Description |
| :--- | :--- | :--- |
| `0x00000000 - 0x00000FFF` | 4 KB | Internal Steppingstone SRAM (NAND Boot Buffer) |
| `0x00001000 - 0x07FFFFFF` | ~128 MB | Bank 0: Boot ROM / Unmapped |
| `0x08000000 - 0x0FFFFFFF` | 128 MB | Bank 1: ROM / Flash |
| `0x10000000 - 0x17FFFFFF` | 128 MB | Bank 2: Peripheral / Expansion |
| `0x30000000 - 0x31FFFFFF` | **32 MB** | **Bank 6: Main System SDRAM** |
| `0x32000000 - 0x33FFFFFF` | 32 MB | Bank 6 SDRAM Mirror / Alias |

---

## 2. Integrated Peripheral MMIO Registers (Samsung S3C2410A)

Reference: *Samsung S3C2410A User's Manual (Rev 1.0, March 2004)*

| Base Address | Peripheral | Key Functions / Registers |
| :--- | :--- | :--- |
| `0x48000000` | **Memory Controller** | `BWSCON`, `BANKCON0-7`, `REFRESH`, `BANKSIZE`, `MRSRB6-7` |
| `0x49000000` | **USB Host Controller** | OHCI compliant USB host registers |
| `0x4A000000` | **Interrupt Controller (INTC)** | `SRCPND`, `INTMOD`, `INTMSK`, `INTPND`, `INTOFFSET`, `INTSUBMSK` |
| `0x4B000000` | **DMA Controller** | 4-channel DMA (`DISRC`, `DIDST`, `DCON`, `DSTAT`) |
| `0x4C000000` | **Clock & Power Management** | `LOCKTIME`, `MPLLCON`, `UPLLCON`, `CLKCON`, `CLKSLOW`, `CLKDIVN` |
| `0x4D000000` | **LCD Controller** | `LCDCON1-5`, `LCDSADDR1-3`, `REDLUT`, `BLUELUT` |
| `0x4E000000` | **NAND Flash Controller** | `NFCONF`, `NFCMD`, `NFADDR`, `NFDATA`, `NFSTAT`, `NFECC` |
| `0x50000000` | **UART (0, 1, 2)** | `ULCON`, `UCON`, `UFCON`, `UTRSTAT`, `UTXH`, `URXH`, `UBRDIV` |
| `0x51000000` | **PWM Timers** | `TCFG0`, `TCFG1`, `TCON`, `TCNTB0-4`, `TCMPB0-3`, `TCNTO0-4` |
| `0x52000000` | **USB Device Controller** | USB function control and endpoint FIFO registers |
| `0x53000000` | **Watchdog Timer (WDT)** | `WTCON`, `WTDAT`, `WTCNT` |
| `0x54000000` | **IIC Bus Interface** | `IICCON`, `IICSTAT`, `IICADD`, `IICDS` |
| `0x55000000` | **IIS Audio Interface** | `IISCON`, `IISMOD`, `IISPSR`, `IISFCON`, `IISFIFO` |
| `0x56000000` | **I/O Ports (GPIO)** | `GPACON` to `GPHCON`, Data and Pull-Up registers |
| `0x57000000` | **Real-Time Clock (RTC)** | `RTCCON`, `TICNT`, `RTCALM`, `BCDSEC-YEAR` |
| `0x58000000` | **ADC & Touch Screen** | `ADCCON`, `ADCTSC`, `ADCDAT0`, `ADCDAT1`, `ADCDLY` |
| `0x59000000` | **SPI Interface** | `SPCON`, `SPSTA`, `SPPIN`, `SPPRE`, `SPTDAT`, `SPRDAT` |
| `0x5A000000` | **SDI (SD / MMC)** | SD Host Interface registers |

---

## 3. NAND Boot Sequence

When the console boots from a NAND cartridge:
1. S3C2410 hardware automatically copies the first **4 KB** of the cartridge into internal SRAM (`0x00000000 - 0x00000FFF`, *Steppingstone*).
2. The ARM920T CPU starts executing at address `0x00000000`.
3. The initial 4 KB bootloader initializes SDRAM at `0x30000000`, enables the NAND controller at `0x4E000000`, and streams U-Boot into SDRAM.
4. It jumps to the entry point in SDRAM (`start_armboot` at `0x30f81b50`) to execute U-Boot.
