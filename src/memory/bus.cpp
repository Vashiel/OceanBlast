#include "bus.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <iomanip>

namespace oceanblast {

Bus::Bus() {
    steppingstone.resize(ADDR_STEPPINGSTONE_SIZE, 0);
    sdram.resize(ADDR_SDRAM_SIZE, 0);
}

Bus::~Bus() {}

void Bus::reset() {
    std::fill(steppingstone.begin(), steppingstone.end(), 0);
    std::fill(sdram.begin(), sdram.end(), 0);
    mmioRegs.clear();

    nfconf = 0;
    nfcmd  = 0;
    nfstat = 0x1; // Ready
    nandAddrCycle = 0;
    nandColAddr = 0;
    nandPageAddr = 0;
    nandByteOffset = 0;
    nandReadActive = false;
    nandReadSpare = false;

    // Reload Steppingstone SRAM from Cartridge NAND Flash
    if (!cartNand.empty()) {
        size_t pageSize = rawNand528 ? 528 : 512;
        size_t dataSize = 512;
        for (size_t p = 0; p < 8 && (p * pageSize + dataSize) <= cartNand.size(); ++p) {
            std::memcpy(steppingstone.data() + (p * dataSize),
                        cartNand.data() + (p * pageSize),
                        dataSize);
        }
    }
}

bool Bus::loadCartridge(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Bus] Error: Could not open cartridge file: " << path << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    cartNand.resize(size);

    if (file.read(reinterpret_cast<char*>(cartNand.data()), size)) {
        rawNand528 = (size % 528 == 0);
        size_t totalPages = rawNand528 ? (size / 528) : (size / 512);

        std::cout << "[Bus] Loaded NAND Cartridge: " << path << " (" << size << " bytes / " 
                  << std::fixed << std::setprecision(1) << (size / (1024.0 * 1024.0)) << " MB)" << std::endl;
        std::cout << "[Bus] NAND Format: " << (rawNand528 ? "Raw 528-byte pages (512 Data + 16 OOB)" : "Flat 512-byte pages")
                  << " Total Pages: " << totalPages << std::endl;

        // Hardware Steppingstone Boot: S3C2410 SoC hardware autonomously copies
        // the first 8 pages of 512 data bytes (4096 bytes total) into Steppingstone boot SRAM at 0x00000000.
        size_t pageSize = rawNand528 ? 528 : 512;
        size_t dataSize = 512;
        for (size_t p = 0; p < 8 && (p * pageSize + dataSize) <= cartNand.size(); ++p) {
            std::memcpy(steppingstone.data() + (p * dataSize),
                        cartNand.data() + (p * pageSize),
                        dataSize);
        }

        std::cout << "[Bus] Steppingstone: Autonomously loaded first 4096 boot bytes into SRAM at 0x00000000." << std::endl;
        return true;
    }
    return false;
}

u8 Bus::read8(u32 addr) {
    addr = translate(addr);
    // 1. Steppingstone SRAM (0x00000000 - 0x00000FFF)
    if (addr < ADDR_STEPPINGSTONE_SIZE) {
        return steppingstone[addr];
    }
    // 2. SDRAM Primary (0x30000000 - 0x31FFFFFF)
    if (addr >= ADDR_SDRAM_BASE && addr < (ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE)) {
        return sdram[addr - ADDR_SDRAM_BASE];
    }
    // Mirror of SDRAM (0x32000000 - 0x33FFFFFF)
    if (addr >= 0x32000000 && addr < (0x32000000 + ADDR_SDRAM_SIZE)) {
        return sdram[addr - 0x32000000];
    }
    // 3. NAND Data Register byte access (0x4E00000C)
    if (addr == 0x4E00000C) {
        return readNandByte();
    }
    // 4. MMIO Registers
    if (addr >= ADDR_MEMCON_BASE && addr < 0x5C000000) {
        u32 val = readMmio(addr & ~3);
        int shift = (addr & 3) * 8;
        return (val >> shift) & 0xFF;
    }
    return 0;
}

u16 Bus::read16(u32 addr) {
    addr = translate(addr);
    if (addr == 0x4E00000C) {
        return readNandByte();
    }
    u16 b0 = read8(addr);
    u16 b1 = read8(addr + 1);
    return b0 | (b1 << 8);
}

u32 Bus::read32(u32 addr) {
    addr = translate(addr);
    // Fast path for 4-byte aligned SDRAM reads
    if (addr >= ADDR_SDRAM_BASE && addr + 4 <= (ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE) && (addr & 3) == 0) {
        u32 val;
        std::memcpy(&val, sdram.data() + (addr - ADDR_SDRAM_BASE), 4);
        return val;
    }
    // Fast path for Steppingstone reads
    if (addr + 4 <= ADDR_STEPPINGSTONE_SIZE && (addr & 3) == 0) {
        u32 val;
        std::memcpy(&val, steppingstone.data() + addr, 4);
        return val;
    }
    // NAND Flash Data Register (0x4E00000C): 8-bit bus clocked once per access
    if (addr == 0x4E00000C) {
        return readNandByte();
    }
    // MMIO Registers
    if (addr >= ADDR_MEMCON_BASE && addr < 0x5C000000) {
        return readMmio(addr);
    }


    u32 b0 = read8(addr);
    u32 b1 = read8(addr + 1);
    u32 b2 = read8(addr + 2);
    u32 b3 = read8(addr + 3);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

void Bus::write8(u32 addr, u8 val) {
    addr = translate(addr);
    if (addr < ADDR_STEPPINGSTONE_SIZE) {
        steppingstone[addr] = val;
        return;
    }
    if (addr >= ADDR_SDRAM_BASE && addr < (ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE)) {
        sdram[addr - ADDR_SDRAM_BASE] = val;
        return;
    }
    if (addr >= 0x32000000 && addr < (0x32000000 + ADDR_SDRAM_SIZE)) {
        sdram[addr - 0x32000000] = val;
        return;
    }

    // Direct byte write to NAND controller registers
    if (addr == 0x4E000004) {
        writeNandCmd(val);
        return;
    }
    if (addr == 0x4E000008) {
        writeNandAddr(val);
        return;
    }

    // MMIO registers
    if (addr >= ADDR_MEMCON_BASE && addr < 0x5C000000) {
        u32 regAddr = addr & ~3;
        u32 cur = readMmio(regAddr);
        int shift = (addr & 3) * 8;
        cur = (cur & ~(0xFF << shift)) | (static_cast<u32>(val) << shift);
        writeMmio(regAddr, cur);
        return;
    }
}

void Bus::write16(u32 addr, u16 val) {
    addr = translate(addr);
    write8(addr, val & 0xFF);
    write8(addr + 1, (val >> 8) & 0xFF);
}

void Bus::write32(u32 addr, u32 val) {
    addr = translate(addr);
    if (addr >= ADDR_SDRAM_BASE && addr + 4 <= (ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE) && (addr & 3) == 0) {
        std::memcpy(sdram.data() + (addr - ADDR_SDRAM_BASE), &val, 4);
        return;
    }
    if (addr + 4 <= ADDR_STEPPINGSTONE_SIZE && (addr & 3) == 0) {
        std::memcpy(steppingstone.data() + addr, &val, 4);
        return;
    }
    if (addr >= ADDR_MEMCON_BASE && addr < 0x5C000000) {
        writeMmio(addr, val);
        return;
    }

    write8(addr, val & 0xFF);
    write8(addr + 1, (val >> 8) & 0xFF);
    write8(addr + 2, (val >> 16) & 0xFF);
    write8(addr + 3, (val >> 24) & 0xFF);
}

static const u8 nand_ecc_table[256] = {
    0x00, 0x55, 0x56, 0x03, 0x59, 0x0c, 0x0f, 0x5a, 0x5a, 0x0f, 0x0c, 0x59, 0x03, 0x56, 0x55, 0x00,
    0x65, 0x30, 0x33, 0x66, 0x3c, 0x69, 0x6a, 0x3f, 0x3f, 0x6a, 0x69, 0x3c, 0x66, 0x33, 0x30, 0x65,
    0x66, 0x33, 0x30, 0x65, 0x3f, 0x6a, 0x69, 0x3c, 0x3c, 0x69, 0x6a, 0x3f, 0x65, 0x30, 0x33, 0x66,
    0x03, 0x56, 0x55, 0x00, 0x5a, 0x0f, 0x0c, 0x59, 0x59, 0x0c, 0x0f, 0x5a, 0x00, 0x55, 0x56, 0x03,
    0x69, 0x3c, 0x3f, 0x6a, 0x30, 0x65, 0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6a, 0x3f, 0x3c, 0x69,
    0x0c, 0x59, 0x5a, 0x0f, 0x55, 0x00, 0x03, 0x56, 0x56, 0x03, 0x00, 0x55, 0x0f, 0x5a, 0x59, 0x0c,
    0x0f, 0x5a, 0x59, 0x0c, 0x56, 0x03, 0x00, 0x55, 0x55, 0x00, 0x03, 0x56, 0x0c, 0x59, 0x5a, 0x0f,
    0x6a, 0x3f, 0x3c, 0x69, 0x33, 0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3c, 0x3f, 0x6a,
    0x6a, 0x3f, 0x3c, 0x69, 0x33, 0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3c, 0x3f, 0x6a,
    0x0f, 0x5a, 0x59, 0x0c, 0x56, 0x03, 0x00, 0x55, 0x55, 0x00, 0x03, 0x56, 0x0c, 0x59, 0x5a, 0x0f,
    0x0c, 0x59, 0x5a, 0x0f, 0x55, 0x00, 0x03, 0x56, 0x56, 0x03, 0x00, 0x55, 0x0f, 0x5a, 0x59, 0x0c,
    0x69, 0x3c, 0x3f, 0x6a, 0x30, 0x65, 0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6a, 0x3f, 0x3c, 0x69,
    0x03, 0x56, 0x55, 0x00, 0x5a, 0x0f, 0x0c, 0x59, 0x59, 0x0c, 0x0f, 0x5a, 0x00, 0x55, 0x56, 0x03,
    0x66, 0x33, 0x30, 0x65, 0x3f, 0x6a, 0x69, 0x3c, 0x3c, 0x69, 0x6a, 0x3f, 0x65, 0x30, 0x33, 0x66,
    0x65, 0x30, 0x33, 0x66, 0x3c, 0x69, 0x6a, 0x3f, 0x3f, 0x6a, 0x69, 0x3c, 0x66, 0x33, 0x30, 0x65,
    0x00, 0x55, 0x56, 0x03, 0x59, 0x0c, 0x0f, 0x5a, 0x5a, 0x0f, 0x0c, 0x59, 0x03, 0x56, 0x55, 0x00
};

static void calculateNandEcc256(const u8* dat, u8* ecc) {
    u8 reg1 = 0, reg2 = 0, reg3 = 0;
    for (int j = 0; j < 256; ++j) {
        u8 idx = nand_ecc_table[dat[j]];
        reg1 ^= (idx & 0x3F);
        if (idx & 0x40) reg3 ^= static_cast<u8>(j);
        if (idx & 0x80) reg2 ^= static_cast<u8>(~j);
    }
    ecc[0] = ~reg1;
    ecc[1] = ~reg2;
    ecc[2] = ~reg3;
}

u8 Bus::readNandByte() {
    if (cartNand.empty()) return 0xFF;

    if (nfcmd == 0x90) { // Read ID
        // Production Nikko digiBLAST cartridges use Toshiba NAND chips (Mfr 0x98):
        // 16 MB: Toshiba TC58128FT (0x98, 0x73, 0x00, 0xFF)
        // 32 MB: Toshiba TC58256FT (0x98, 0x75, 0x00, 0xFF)
        // 64 MB: Toshiba TC58512FT (0x98, 0x76, 0x00, 0xFF)
        // 128 MB / Dev: Samsung K9K1G08U0M (0xEC, 0x79, 0xA5, 0xC0)
        u8 mfrId = 0x98; // Toshiba
        u8 devId = 0x73; // 16 MB
        u8 id3   = 0x00;
        u8 id4   = 0xFF;

        if (cartNand.size() > 65 * 1024 * 1024) {
            devId = 0x76; // 64 MB (e.g. Cuccioli)
        } else if (cartNand.size() > 30 * 1024 * 1024) {
            devId = 0x75; // 32 MB (e.g. Gormiti)
        } else if (cartNand.size() > 100 * 1024 * 1024) {
            mfrId = 0xEC;
            devId = 0x79;
            id3   = 0xA5;
            id4   = 0xC0;
        }

        u8 ret = 0xFF;
        switch (nandByteOffset) {
            case 0: ret = mfrId; break;
            case 1: ret = devId; break;
            case 2: ret = id3;   break;
            case 3: ret = id4;   break;
            default: ret = 0xFF; break;
        }
        nandByteOffset++;
        return ret;
    }

    if (nandReadActive) {
        size_t pageSize = rawNand528 ? 528 : 512;
        size_t baseOffset = nandPageAddr * pageSize;

        // Check if this page in the dump contains real OOB data
        bool pageHasRawOob = false;
        if (rawNand528 && (baseOffset + 528 <= cartNand.size())) {
            for (size_t k = 0; k < 16; ++k) {
                if (cartNand[baseOffset + 512 + k] != 0xFF) {
                    pageHasRawOob = true;
                    break;
                }
            }
        }

        u8 b = 0xFF;
        if (nandReadSpare) {
            u32 oobCol = nandColAddr;
            if (pageHasRawOob) {
                // Return authentic raw OOB bytes directly from the dump
                if (baseOffset + 512 + oobCol < cartNand.size()) {
                    b = cartNand[baseOffset + 512 + oobCol];
                }
            } else if (baseOffset + 512 <= cartNand.size()) {
                // For dumps with erased/blanked OOB (0xFF), dynamically synthesize
                // standard Linux MTD 256-byte 1-bit Hamming ECC to allow U-Boot read verification
                u8 ecc1[3], ecc2[3];
                calculateNandEcc256(cartNand.data() + baseOffset, ecc1);
                calculateNandEcc256(cartNand.data() + baseOffset + 256, ecc2);
                if (oobCol < 3) b = ecc1[oobCol];
                else if (oobCol < 6) b = ecc2[oobCol - 3];
                else b = 0xFF;
            }
            nandColAddr++;
            if (nandColAddr >= 16) {
                nandColAddr = 0;
                nandPageAddr++;
            }
            return b;
        } else {
            if (nandColAddr < 512) {
                if (baseOffset + nandColAddr < cartNand.size()) {
                    b = cartNand[baseOffset + nandColAddr];
                }
            } else if (nandColAddr < 528) {
                u32 oobCol = nandColAddr - 512;
                if (pageHasRawOob) {
                    if (baseOffset + 512 + oobCol < cartNand.size()) {
                        b = cartNand[baseOffset + 512 + oobCol];
                    }
                } else if (baseOffset + 512 <= cartNand.size()) {
                    u8 ecc1[3], ecc2[3];
                    calculateNandEcc256(cartNand.data() + baseOffset, ecc1);
                    calculateNandEcc256(cartNand.data() + baseOffset + 256, ecc2);
                    if (oobCol < 3) b = ecc1[oobCol];
                    else if (oobCol < 6) b = ecc2[oobCol - 3];
                    else b = 0xFF;
                }
            }

            nandColAddr++;
            if (nandColAddr >= 528) {
                nandColAddr = 0;
                nandPageAddr++;
            }
            return b;
        }
    }

    return 0xFF;
}


void Bus::writeNandCmd(u8 cmd) {
    nfcmd = cmd;
    nandAddrCycle = 0;
    nandColAddr = 0;
    nandPageAddr = 0;
    nandByteOffset = 0;

    switch (cmd) {
        case 0x00: // Read 1st half page (col 0..255)
            nandReadActive = true;
            nandReadSpare = false;
            nandColAddr = 0;
            break;
        case 0x01: // Read 2nd half page (col 256..511)
            nandReadActive = true;
            nandReadSpare = false;
            nandColAddr = 256;
            break;
        case 0x50: // Read Spare Area (col 0..15 in OOB)
            nandReadActive = true;
            nandReadSpare = true;
            nandColAddr = 0;
            break;
        case 0x90: // Read ID
            nandReadActive = false;
            nandByteOffset = 0;
            break;
        case 0xFF: // Reset
            nandReadActive = false;
            nandReadSpare = false;
            break;
        default:
            break;
    }
}

void Bus::writeNandAddr(u8 addr) {
    if (nfcmd == 0x90) {
        nandByteOffset = 0;
        return;
    }

    if (nfcmd == 0x50) { // Read Spare: Address cycles are Row (Page) address only
        switch (nandAddrCycle) {
            case 0:
                nandPageAddr = (nandPageAddr & ~0xFF) | addr;
                break;
            case 1:
                nandPageAddr = (nandPageAddr & ~0xFF00) | (static_cast<u32>(addr) << 8);
                break;
            case 2:
                nandPageAddr = (nandPageAddr & ~0xFF0000) | (static_cast<u32>(addr) << 16);
                break;
            default:
                break;
        }
        nandAddrCycle++;
        return;
    }

    switch (nandAddrCycle) {
        case 0: // Column Address A0..A7
            nandColAddr = addr;
            break;
        case 1: // Page Address A9..A16 (Row Byte 1)
            nandPageAddr = (nandPageAddr & ~0xFF) | addr;
            break;
        case 2: // Page Address A17..A24 (Row Byte 2)
            nandPageAddr = (nandPageAddr & ~0xFF00) | (static_cast<u32>(addr) << 8);
            break;
        case 3: // Page Address A25 (Row Byte 3, for devices > 16MB)
            nandPageAddr = (nandPageAddr & ~0xFF0000) | (static_cast<u32>(addr) << 16);
            break;
        default:
            break;
    }
    nandAddrCycle++;
}


u32 Bus::readMmio(u32 addr) {
    switch (addr) {
        // NAND Flash Controller (0x4E000000)
        case 0x4E000000: return nfconf;
        case 0x4E000004: return nfcmd;
        case 0x4E000008: return 0;
        case 0x4E00000C: return readNandByte();
        case 0x4E000010: return nfstat | 0x1; // Bit 0: Ready

        // UART 0 (0x50000000)
        case 0x50000010: return 0x06; // UTRSTAT0: Transmitter empty (bit 2) & Transmit buffer empty (bit 1)
        case 0x50000014: return 0x00; // UERSTAT0
        case 0x50000018: return 0x00; // UFSTAT0
        case 0x5000001C: return 0x00; // UMSTAT0

        // Interrupt Controller (0x4A000000)
        case 0x4A000000: return mmioRegs[0x4A000000]; // SRCPND
        case 0x4A000008: return mmioRegs[0x4A000008]; // INTMSK
        case 0x4A000010: return mmioRegs[0x4A000010]; // INTPND
        case 0x4A000014: return mmioRegs[0x4A000014]; // INTOFFSET
        case 0x4A00001C: return mmioRegs[0x4A00001C]; // INTSUBMSK

        // Clock & Power (0x4C000000)
        case 0x4C000014: return mmioRegs[0x4C000014]; // CLKDIVN

        // PWM Timers (0x51000000)
        case 0x51000040: { // TCNTO4 (Timer 4 Count Observation Register)
            timer4Cnt -= 64;
            return timer4Cnt;
        }

        // Watchdog Timer (0x53000000)
        case 0x53000000: return mmioRegs[0x53000000]; // WTCON

        // ADC Controller (0x58000000)
        case 0x58000000: return (mmioRegs[0x58000000] & ~0x1) | 0x8000; // ADCCON: Bit 0 (ENABLE_START) cleared, Bit 15 (ECFLG) conversion complete
        case 0x58000004: return mmioRegs[0x58000004]; // ADCTSC
        case 0x58000008: return mmioRegs[0x58000008]; // ADCDLY
        case 0x5800000C: return 750; // ADCDAT0: Normal battery voltage level (750 counts, within 620..830 boot window)

        // GPIO & System Status Registers (0x56000000)
        case 0x560000B0: return 0x32410002; // GSTATUS1: S3C2410A Chip ID
        case 0x560000B4: return 0x00000001; // GSTATUS2: Power-on reset flag

        default: {
            auto it = mmioRegs.find(addr);
            return (it != mmioRegs.end()) ? it->second : 0;
        }
    }
}

void Bus::writeMmio(u32 addr, u32 val) {
    mmioRegs[addr] = val;

    switch (addr) {
        // NAND Flash Controller
        case 0x4E000000:
            nfconf = val;
            break;
        case 0x4E000004:
            writeNandCmd(val & 0xFF);
            break;
        case 0x4E000008:
            writeNandAddr(val & 0xFF);
            break;
        case 0x4E00000C:
            break;

        // UART 0 TX FIFO / Buffer (0x50000020 or 0x50000023)
        case 0x50000020:
        case 0x50000024: {
            char ch = static_cast<char>(val & 0xFF);
            std::cout << ch << std::flush;
            break;
        }

        default:
            break;
    }
}

u32 Bus::translate(u32 va) const {
    if (!mmuEnabled) return va;

    // Check TTB first-level translation table if mapped in SDRAM
    u32 ttbBase = ttb & ~0x3FFF;
    if (ttbBase >= ADDR_SDRAM_BASE && ttbBase + 16384 <= ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE) {
        u32 index = (va >> 20) & 0xFFF;
        u32 descOffset = ttbBase - ADDR_SDRAM_BASE + (index * 4);
        u32 desc = 0;
        std::memcpy(&desc, sdram.data() + descOffset, 4);

        if ((desc & 3) == 2) { // 1 MB Section Descriptor
            return (desc & 0xFFF00000) | (va & 0x000FFFFF);
        } else if ((desc & 3) == 1) { // Coarse Page Table
            u32 ptPhys = desc & ~0x3FF;
            if (ptPhys >= ADDR_SDRAM_BASE && ptPhys + 1024 <= ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE) {
                u32 ptIndex = (va >> 12) & 0xFF;
                u32 ptOffset = ptPhys - ADDR_SDRAM_BASE + (ptIndex * 4);
                u32 pte = 0;
                std::memcpy(&pte, sdram.data() + ptOffset, 4);
                if ((pte & 3) == 2) { // Small Page (4KB)
                    return (pte & 0xFFFFF000) | (va & 0xFFF);
                }
            }
        }
    }

    // Direct kernel linear SDRAM mapping (0xC0000000 -> 0x30000000)
    if (va >= 0xC0000000 && va < (0xC0000000 + ADDR_SDRAM_SIZE)) {
        return (va - 0xC0000000) + ADDR_SDRAM_BASE;
    }

    // Linux S3C24XX virtual MMIO mappings (0xF0000000 .. 0xF1600000)
    if (va >= 0xF0000000 && va < 0xF1600000) {
        u32 periph = va & 0xFFF00000;
        u32 reg = va & 0x000FFFFF;
        switch (periph) {
            case 0xF0000000: return ADDR_INTCON_BASE + reg;   // Interrupt Controller (0x4A000000)
            case 0xF0100000: return ADDR_MEMCON_BASE + reg;   // Memory Controller (0x48000000)
            case 0xF0200000: return ADDR_USBHOST_BASE + reg;  // USB Host (0x49000000)
            case 0xF0400000: return ADDR_CLKCON_BASE + reg;   // Clock & Power (0x4C000000)
            case 0xF0600000: return ADDR_LCDCON_BASE + reg;   // LCD Controller (0x4D000000)
            case 0xF0800000: return ADDR_UART_BASE + reg;     // UART0..2 (0x50000000)
            case 0xF0900000: return ADDR_TIMER_BASE + reg;    // PWM Timers (0x51000000)
            case 0xF0A00000: return ADDR_USBD_BASE + reg;     // USB Device (0x52000000)
            case 0xF0B00000: return ADDR_WDT_BASE + reg;      // Watchdog Timer (0x53000000)
            case 0xF0C00000: return ADDR_IIC_BASE + reg;      // I2C (0x54000000)
            case 0xF0D00000: return ADDR_IIS_BASE + reg;      // IIS Audio (0x55000000)
            case 0xF0E00000: return ADDR_GPIO_BASE + reg;     // GPIO (0x56000000)
            case 0xF0F00000: return ADDR_RTC_BASE + reg;      // RTC (0x57000000)
            case 0xF1000000: return ADDR_ADC_BASE + reg;      // ADC (0x58000000)
            case 0xF1100000: return ADDR_SPI_BASE + reg;      // SPI (0x59000000)
            case 0xF1200000: return ADDR_SDI_BASE + reg;      // SD/MMC (0x5A000000)
            default: break;
        }
    }

    return va;
}

} // namespace oceanblast
