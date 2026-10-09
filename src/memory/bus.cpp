#include "bus.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <iomanip>

namespace oceanblast {

Bus::Bus() {
    steppingstone.resize(ADDR_STEPPINGSTONE_SIZE, 0);
    sdram.resize(ADDR_SDRAM_SIZE, 0);
    sdramPtr = sdram.data();
    mmioRegs[0x4C000004] = 0x5c080;
    mmioRegs[0x4C000010] = 4;
    mmioRegs[0x4C000014] = 0;
}

Bus::~Bus() {}

bool Bus::saveMmioProfile(const std::string& path) const {
    std::ofstream output(path);
    output << "physical_address,guest_reads,guest_writes\n";
    for (const auto& item : mmioAccesses)
        output << "0x" << std::hex << item.first << std::dec << ','
               << item.second.reads << ',' << item.second.writes << '\n';
    output.flush();
    return static_cast<bool>(output);
}

bool Bus::loadEeprom(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::array<u8, 2048> image;
    if (!file || file.tellg() != std::streampos(image.size())) return false;
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(image.data()), image.size())) return false;
    eeprom.restore(image);
    return true;
}

bool Bus::saveEeprom(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    const auto& image = eeprom.contents();
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
    file.flush();
    return static_cast<bool>(file);
}

void Bus::reset() {
    sdramPtr = sdram.data();
    std::fill(steppingstone.begin(), steppingstone.end(), 0);
    std::fill(sdram.begin(), sdram.end(), 0);
    mmioRegs.clear();
    mmioAccesses.clear();
    clocks.reset();
    mmioRegs[0x4C000004] = 0x5c080;
    mmioRegs[0x4C000010] = 4;
    mmioRegs[0x4C000014] = 0;
    mmioRegs[0x4A000008] = 0xFFFFFFFF; // INTMSK default: all masked
    mmioRegs[0x4A00001C] = 0x000007FF; // INTSUBMSK default: all sub-masked
    mmioRegs[0x56000054] = 0x000000FF; // GPFDAT default: all pulled up
    mmioRegs[0x56000064] = 0x0000FFFF; // GPGDAT default: all pulled up
    mmioRegs[0x560000A4] = 0xFFFFFFF0; // EINTMASK default: all ext masked
    mmioRegs[0x560000A8] = 0x00000000; // EINTPEND default: clear
    buttonMask = 0;
    uart0TxLevelActive = false;
    timer4.reset();
    regTcon = 0;
    regSrcpnd = 0;
    regIntpnd = 0;
    regIntmsk = ~0u;
    dma2Active = false;
    dma2Paused = false;
    dma2RedundantEnables = 0;
    dma2Timer = 0;
    dma2PcmPending.clear();
    adcPending = false;
    adcTimer = 0;
    i2cPending = false;
    i2cTimer = 0;
    i2cAddressPhase = false;
    eeprom.resetBus();

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

        // Preload boot splash from NAND (page 384) into SDRAM 0x30300000 and 0x30310000 for immediate display
        if (cartNand.size() >= (384 + 128) * pageSize) {
            for (size_t p = 0; p < 128; ++p) {
                size_t srcOff = (384 + p) * pageSize;
                size_t dstOff0 = 0x300000 + (p * 512);
                size_t dstOff1 = 0x310000 + (p * 512);
                if (srcOff + 512 <= cartNand.size()) {
                    if (dstOff0 + 512 <= ADDR_SDRAM_SIZE) {
                        std::memcpy(sdram.data() + dstOff0, cartNand.data() + srcOff, 512);
                    }
                    if (dstOff1 + 512 <= ADDR_SDRAM_SIZE) {
                        std::memcpy(sdram.data() + dstOff1, cartNand.data() + srcOff, 512);
                    }
                }
            }
        }

        return true;
    }
    return false;
}

u8 Bus::read8PhysSlow(u32 addr) {
    // 1. Steppingstone SRAM (0x00000000 - 0x00000FFF)
    if (addr < ADDR_STEPPINGSTONE_SIZE) {
        return steppingstone[addr];
    }
    // 2. SDRAM Primary (0x30000000 - 0x31FFFFFF)
    if (addr >= ADDR_SDRAM_BASE && (addr - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE) {
        return sdram[addr - ADDR_SDRAM_BASE];
    }
    // Mirror of SDRAM (0x32000000 - 0x33FFFFFF)
    if (addr >= 0x32000000 && (addr - 0x32000000) < ADDR_SDRAM_SIZE) {
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

bool Bus::peek8(u32 va, u8& val) const {
    MmuFault fault = MmuFault::NONE;
    u32 pa = translate(va, &fault);
    if (fault != MmuFault::NONE) return false;
    val = const_cast<Bus*>(this)->read8Phys(pa);
    return true;
}

bool Bus::peek32(u32 va, u32& val) const {
    MmuFault fault = MmuFault::NONE;
    u32 pa = translate(va, &fault);
    if (fault != MmuFault::NONE) return false;
    if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0) {
        std::memcpy(&val, sdram.data() + (pa - ADDR_SDRAM_BASE), 4);
        return true;
    }
    if (pa <= (ADDR_STEPPINGSTONE_SIZE - 4) && (pa & 3) == 0) {
        std::memcpy(&val, steppingstone.data() + pa, 4);
        return true;
    }
    u8 b0 = const_cast<Bus*>(this)->read8Phys(pa);
    u8 b1 = const_cast<Bus*>(this)->read8Phys(pa + 1);
    u8 b2 = const_cast<Bus*>(this)->read8Phys(pa + 2);
    u8 b3 = const_cast<Bus*>(this)->read8Phys(pa + 3);
    val = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    return true;
}

void Bus::write8PhysSlow(u32 addr, u8 val) {
    if (addr < ADDR_STEPPINGSTONE_SIZE) {
        steppingstone[addr] = val;
        return;
    }
    if (addr >= ADDR_SDRAM_BASE && (addr - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE) {
        sdram[addr - ADDR_SDRAM_BASE] = val;
        return;
    }
    if (addr >= 0x32000000 && (addr - 0x32000000) < ADDR_SDRAM_SIZE) {
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

u16 Bus::read16PhysSlow(u32 pa) {
    if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 2) && (pa & 1) == 0) {
        u16 val;
        std::memcpy(&val, sdram.data() + (pa - ADDR_SDRAM_BASE), 2);
        return val;
    }
    if (pa <= (ADDR_STEPPINGSTONE_SIZE - 2) && (pa & 1) == 0) {
        u16 val;
        std::memcpy(&val, steppingstone.data() + pa, 2);
        return val;
    }
    if (pa == 0x4E00000C) return readNandByte();
    u16 b0 = read8Phys(pa);
    u16 b1 = read8Phys(pa + 1);
    return b0 | (b1 << 8);
}

u32 Bus::read32PhysSlow(u32 pa) {
    if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0) {
        u32 val;
        std::memcpy(&val, sdram.data() + (pa - ADDR_SDRAM_BASE), 4);
        return val;
    }
    if (pa <= (ADDR_STEPPINGSTONE_SIZE - 4) && (pa & 3) == 0) {
        u32 val;
        std::memcpy(&val, steppingstone.data() + pa, 4);
        return val;
    }
    if (pa == 0x4E00000C) return readNandByte();
    if (pa >= ADDR_MEMCON_BASE && pa < 0x5C000000) return readMmio(pa);

    u32 b0 = read8Phys(pa);
    u32 b1 = read8Phys(pa + 1);
    u32 b2 = read8Phys(pa + 2);
    u32 b3 = read8Phys(pa + 3);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

void Bus::write16PhysSlow(u32 pa, u16 val) {
    write8Phys(pa, val & 0xFF);
    write8Phys(pa + 1, (val >> 8) & 0xFF);
}

void Bus::write32PhysSlow(u32 pa, u32 val) {
    if (pa == 0x30207fec) {
        static int wpCount = 0;
        if (wpCount++ < 10) {
            std::cout << "[WATCHPOINT 0x30207fec WRITE #" << wpCount << "] val=0x" << std::hex << val << std::dec << std::endl;
        }
    }
    if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0) {
        std::memcpy(sdram.data() + (pa - ADDR_SDRAM_BASE), &val, 4);
        return;
    }
    if (pa <= (ADDR_STEPPINGSTONE_SIZE - 4) && (pa & 3) == 0) {
        std::memcpy(steppingstone.data() + pa, &val, 4);
        return;
    }
    if (pa >= ADDR_MEMCON_BASE && pa < 0x5C000000) {
        writeMmio(pa, val);
        return;
    }

    write8Phys(pa, val & 0xFF);
    write8Phys(pa + 1, (val >> 8) & 0xFF);
    write8Phys(pa + 2, (val >> 16) & 0xFF);
    write8Phys(pa + 3, (val >> 24) & 0xFF);
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


u32 Bus::readMmio(u32 addr, bool guestAccess) {
    if (mmioProfiling && guestAccess) ++mmioAccesses[addr].reads;
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

        // DMA Controller Channel 2 (0x4B000080 - IIS Audio)
        case 0x4B000094: // DSTAT2
        case 0x4B000098: // DCSRC2: current source address, used by ALSA's PCM pointer
        case 0x4B00009C: { // DCDST2
            if (!dma2Active || !dma2Period) return mmioRegs[addr];
            const u32 totalItems = dma2Count / dma2ItemSize;
            const u32 done = uint64_t(totalItems) * (dma2Period - dma2Timer) / dma2Period;
            if (addr == 0x4B000094) return totalItems - done;
            if (addr == 0x4B000098) return dma2Src + (dma2SrcFixed ? 0 : done * dma2ItemSize);
            return dma2Dst + (dma2DstFixed ? 0 : done * dma2ItemSize);
        }
        case 0x4B0000A0: return mmioRegs[0x4B0000A0]; // DMASKTRIG2

        // Interrupt Controller (0x4A000000)
        case 0x4A000000: return regSrcpnd;
        case 0x4A000008: return regIntmsk;
        case 0x4A000010: return regIntpnd;
        case 0x4A000014: return mmioRegs[0x4A000014]; // INTOFFSET
        case 0x4A00001C: return mmioRegs[0x4A00001C]; // INTSUBMSK

        // Clock & Power (0x4C000000)
        case 0x4C000014: return mmioRegs[0x4C000014]; // CLKDIVN

        // PWM Timers (0x51000000)
        case 0x51000008: return regTcon;
        case 0x51000040: return timer4.observe(); // Read-only countdown observation.

        // Watchdog Timer (0x53000000)
        case 0x53000000: return mmioRegs[0x53000000]; // WTCON

        // I2C Controller (0x54000000)
        case 0x54000000: return mmioRegs[0x54000000]; // IICCON
        case 0x54000004: return mmioRegs[0x54000004]; // IICSTAT
        case 0x54000008: return mmioRegs[0x54000008]; // IICADD
        case 0x5400000C: return mmioRegs[0x5400000C]; // IICDS

        // ADC Controller (0x58000000)
        case 0x58000000: return (mmioRegs[0x58000000] & ~0x1) | (1 << 15); // ADCCON: Bit 0 cleared, Bit 15 (ECFLG) conversion complete
        case 0x58000004: return mmioRegs[0x58000004]; // ADCTSC
        case 0x58000008: return mmioRegs[0x58000008]; // ADCDLY
        case 0x5800000C: return 750; // ADCDAT0: Normal battery voltage level (750 counts)
        case 0x58000010: return 750; // ADCDAT1

        // GPIO & System Status Registers (0x56000000)
        case 0x56000054: { // GPFDAT: Active-low inputs for buttons
            u32 gpf = 0xFF;
            if (buttonMask & (1 << 4)) gpf &= ~(1 << 0); // BTN_A -> GPF0
            if (buttonMask & (1 << 5)) gpf &= ~(1 << 1); // BTN_B -> GPF1
            if (buttonMask & (1 << 0)) gpf &= ~(1 << 2); // BTN_UP -> GPF2
            if (buttonMask & (1 << 2)) gpf &= ~(1 << 3); // BTN_LEFT -> GPF3
            if (buttonMask & (1 << 3)) gpf &= ~(1 << 6); // BTN_RIGHT -> GPF6
            if (buttonMask & (1 << 1)) gpf &= ~(1 << 7); // BTN_DOWN -> GPF7
            return gpf;
        }
        case 0x56000064: { // GPGDAT: Active-low inputs for buttons
            u32 gpg = 0xFFFF;
            if (buttonMask & (1 << 7)) gpg &= ~(1 << 8);  // BTN_R -> GPG8
            if (buttonMask & (1 << 9)) gpg &= ~(1 << 9);  // BTN_SELECT -> GPG9
            if (buttonMask & (1 << 8)) gpg &= ~(1 << 10); // BTN_START -> GPG10
            if (buttonMask & (1 << 6)) gpg &= ~(1 << 11); // BTN_L -> GPG11
            return gpg;
        }
        case 0x560000A4: { // EINTMASK
            auto it = mmioRegs.find(0x560000A4);
            return (it != mmioRegs.end()) ? it->second : 0xFFFFFFF0;
        }
        case 0x560000A8: { // EINTPEND
            auto it = mmioRegs.find(0x560000A8);
            return (it != mmioRegs.end()) ? it->second : 0;
        }
        case 0x560000B0: return 0x32410002; // GSTATUS1: S3C2410A Chip ID
        case 0x560000B4: return 0x00000001; // GSTATUS2: Power-on reset flag

        default: {
            auto it = mmioRegs.find(addr);
            return (it != mmioRegs.end()) ? it->second : 0;
        }
    }
}

void Bus::writeMmio(u32 addr, u32 val) {
    if (mmioProfiling) ++mmioAccesses[addr].writes;
    if (i2cLogging && addr >= ADDR_IIC_BASE && addr <= ADDR_IIC_BASE + 0x0c)
        std::cout << "[I2C WRITE] register=0x" << std::hex << addr << " value=0x" << val
                  << " control=0x" << mmioRegs[ADDR_IIC_BASE] << " status=0x" << mmioRegs[ADDR_IIC_BASE + 4]
                  << " data=0x" << mmioRegs[ADDR_IIC_BASE + 12] << std::dec << '\n';
    switch (addr) {
        // S3C2410 Interrupt Controller (W1C registers)
        case 0x4A000000: // SRCPND: Write 1 to clear
            regSrcpnd &= ~val;
            mmioRegs[0x4A000000] = regSrcpnd;
            return;
        case 0x4A000008: // INTMSK
            regIntmsk = val;
            mmioRegs[0x4A000008] = val;
            updateUart0TxInterrupt();
            selectPendingIrq();
            return;
        case 0x4A000010: { // INTPND: Write 1 to clear
            regIntpnd &= ~val;
            selectPendingIrq();
            mmioRegs[0x4A000010] = regIntpnd;
            return;
        }
        case 0x4A000018: // SUBSRCPND: Write 1 to clear
            mmioRegs[0x4A000018] &= ~val;
            updateUart0TxInterrupt();
            return;
        case 0x4A00001C: // INTSUBMSK
            mmioRegs[addr] = val;
            updateUart0TxInterrupt();
            return;
        case 0x50000004: { // UCON0: transmit mode and interrupt trigger
            const bool wasIrqMode = ((mmioRegs[addr] >> 2) & 3u) == 1u;
            mmioRegs[addr] = val;
            updateUart0TxInterrupt(!wasIrqMode);
            return;
        }

        // PWM Timers
        case 0x51000000: // TCFG0: timer 2..4 prescaler.
        case 0x51000004: // TCFG1: timer 4 input divider.
            mmioRegs[addr] = val;
            timer4.configure(mmioRegs[0x51000000], mmioRegs[0x51000004]);
            return;
        case 0x5100003C: // TCNTB4: applied on manual update or reload.
            mmioRegs[addr] = val;
            timer4.setBuffer(val);
            return;
        case 0x51000008: // TCON
            timer4.control(val);
            regTcon = val;
            mmioRegs[0x51000008] = val;
            return;

        // I2C Controller (0x54000000)
        case 0x54000000: // IICCON
            if ((mmioRegs[addr] & 0x10) && !(val & 0x10) && (mmioRegs[0x54000004] & 0x20)) {
                i2cPending = true;
                i2cTimer = 50;
            }
            mmioRegs[0x54000000] = val;
            return;
        case 0x54000004: // IICSTAT
            mmioRegs[0x54000004] = (val & ~1u) | (mmioRegs[0x54000004] & 1u);
            if ((val & 0x30) == 0x30) { // START or repeated START.
                i2cAddressPhase = true;
                i2cPending = true;
                i2cTimer = 50; // Complete transfer in 50 cycles
            } else {
                eeprom.stop();
                i2cPending = false;
                i2cTimer = 0;
            }
            return;
        case 0x54000008: // IICADD
            mmioRegs[0x54000008] = val;
            return;
        case 0x5400000C: // IICDS
            mmioRegs[0x5400000C] = val;
            return;

        case 0x4C000000: // LOCKTIME
        case 0x4C000008: // UPLLCON
        case 0x4C00000C: // CLKCON
            mmioRegs[addr] = val;
            return;
        case 0x4C000004: // MPLLCON
            flushAudioClockBoundary();
            clocks.setMpll(val); mmioRegs[addr] = val; updateClockedDevices(); return;
        case 0x4C000010: // CLKSLOW
            flushAudioClockBoundary();
            clocks.setSlow(val); mmioRegs[addr] = val; updateClockedDevices(); return;
        case 0x4C000014: // CLKDIVN
            flushAudioClockBoundary();
            clocks.setDivider(val); mmioRegs[addr] = val; updateClockedDevices(); return;

        // IIS Audio Controller (0x55000000)
        case 0x55000000: // IISCON
        case 0x55000004: // IISMOD
        case 0x55000008: // IISPSR
        case 0x5500000C: // IISFCON
        case 0x55000010: // IISFIFO
            if (addr != 0x55000010 && addr != 0x5500000C) flushAudioClockBoundary();
            mmioRegs[addr] = val;
            if (addr != 0x55000010 && addr != 0x5500000C) updateClockedDevices();
            return;

        // S3C2410 DMA Channel 2 (IIS Audio)
        case 0x4B0000A0: { // DMASKTRIG2
            mmioRegs[0x4B0000A0] = val;
            if (val & (1 << 2)) { // STOP
                if (audioCallback && dma2PcmPending.size() >= 2) {
                    audioCallback(dma2PcmPending.data(), dma2PcmPending.size() & ~size_t(1));
                }
                dma2Active = false;
                dma2Timer = 0;
                dma2PcmPending.clear();
                mmioRegs[0x4B000094] = 0;
            } else if (val & (1 << 1)) { // ON
                if (dma2Active) { ++dma2RedundantEnables; return; }
                dma2Active = true;
                dma2Src = mmioRegs[0x4B000080]; // DISRC2
                u32 dcon = mmioRegs[0x4B000090]; // DCON2
                u32 tc = dcon & 0x000FFFFF;      // Transfer Count
                u32 dsz = (dcon >> 20) & 3;      // Data size (00=1B, 01=2B, 10=4B)
                u32 itemSize = (dsz == 1) ? 2 : ((dsz == 2) ? 4 : 1);
                dma2Count = tc * itemSize;       // Total bytes
                dma2ItemSize = itemSize;
                dma2Dst = mmioRegs[0x4B000088];
                dma2SrcFixed = (mmioRegs[0x4B000084] & 1) != 0;
                dma2DstFixed = (mmioRegs[0x4B00008C] & 1) != 0;
                mmioRegs[0x4B000098] = dma2Src;
                mmioRegs[0x4B00009C] = dma2Dst;
                mmioRegs[0x4B000094] = tc;
                // In S3C2410 IIS audio, DMA rate is paced by DAC consumption.
                // Dynamic cycle calculation prevents ALSA from racing ahead or stalling.
                u32 rate = getAudioSampleRate();
                u32 bytesPerSec = rate * 4; // 16-bit stereo PCM
                dma2Timer = (dma2Count > 0 && bytesPerSec > 0)
                    ? std::max<uint64_t>(1, uint64_t(dma2Count) * 20000000 / bytesPerSec) : 150000;
                dma2Period = dma2Timer;
                dma2EmittedBytes = 0;
                dma2PcmPending.clear();
                scheduleDma2Audio();
            }
            return;
        }

        // ADC Controller (0x58000000)
        case 0x58000000: { // ADCCON
            mmioRegs[0x58000000] = val;
            if (val & 1) { // ENABLE_START
                adcPending = true;
                adcTimer = 20; // Complete conversion in 20 cycles
            }
            return;
        }

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

        // UART0 transmit holding register; URXH at +0x24 is receive-only.
        case 0x50000020: {
            char ch = static_cast<char>(val & 0xFF);
            std::cout << ch << std::flush;
            updateUart0TxInterrupt(true);
            break;
        }
        case 0x50000024: return;

        // S3C2410 GPIO & External Interrupt Registers
        case 0x560000A4: // EINTMASK
            mmioRegs[0x560000A4] = val;
            return;
        case 0x560000A8: // EINTPEND (W1C: Write 1 to clear)
            mmioRegs[0x560000A8] &= ~val;
            return;

        default:
            mmioRegs[addr] = val;
            break;
    }
}

void Bus::updateUart0TxInterrupt(bool emptyTransition) {
    // The existing UART sink consumes each byte immediately. Its transmit FIFO
    // remains empty, so IRQ mode must expose that condition to the guest driver.
    const u32 control = mmioRegs[0x50000004];
    const bool irqMode = ((control >> 2) & 3u) == 1u;
    const bool levelMode = (control & (1u << 9)) != 0;
    const u32 txBit = 1u << 1; // SUBSRCPND TXD0 -> main INT_UART0 (28)
    const bool subUnmasked = (mmioRegs[0x4A00001C] & txBit) == 0;
    uart0TxLevelActive = irqMode && levelMode && subUnmasked;
    if (irqMode && (levelMode || emptyTransition))
        mmioRegs[0x4A000018] |= txBit;
    if (subUnmasked && (mmioRegs[0x4A000018] & txBit))
        requestIrq(28);
}

void Bus::requestIrq(u32 bit) {
    regSrcpnd |= (1 << bit);
    mmioRegs[0x4A000000] = regSrcpnd;
    selectPendingIrq();
}

void Bus::selectPendingIrq() {
    if (regIntpnd) return; // Keep the selected source until acknowledgement.
    const u32 pending = regSrcpnd & ~regIntmsk;
    u32 selected = 0;
    if (pending) {
        while (!(pending & (1u << selected))) ++selected;
        regIntpnd = 1u << selected;
    }
    mmioRegs[0x4A000010] = regIntpnd;
    mmioRegs[0x4A000014] = selected;
}

void Bus::scheduleDma2Audio() {
    // Snapshot each consumed 16-bit sample before the producer can reuse RAM.
    // Host callbacks remain batched to avoid submitting tiny audio fragments.
    const u32 next = std::min(dma2Count, dma2EmittedBytes + std::max(2u, dma2ItemSize));
    dma2NextAudioTimer = dma2Period - std::min<uint64_t>(dma2Period,
        (uint64_t(next) * dma2Period + dma2Count - 1) / std::max(dma2Count, 1u));
}

void Bus::streamDma2Audio(bool complete) {
    const u32 consumed = complete ? dma2Count : u32(uint64_t(dma2Count) * (dma2Period - dma2Timer) / dma2Period);
    const u32 quantum = std::max(2u, dma2ItemSize);
    const u32 end = consumed / quantum * quantum;
    if (end > dma2EmittedBytes && audioCallback && dma2Dst == 0x55000010 &&
        dma2Src >= ADDR_SDRAM_BASE && uint64_t(dma2Src - ADDR_SDRAM_BASE) + dma2Count <= ADDR_SDRAM_SIZE) {
        if (!dma2SrcFixed) {
            const auto* pcm = reinterpret_cast<const int16_t*>(sdram.data() + dma2Src - ADDR_SDRAM_BASE + dma2EmittedBytes);
            dma2PcmPending.insert(dma2PcmPending.end(), pcm, pcm + (end - dma2EmittedBytes) / 2);
        } else {
            std::vector<int16_t> pcm((end - dma2EmittedBytes) / 2);
            const auto* source = sdram.data() + dma2Src - ADDR_SDRAM_BASE;
            for (size_t i = 0; i < pcm.size(); ++i) {
                const size_t offset = ((dma2EmittedBytes + i * 2) % dma2ItemSize);
                pcm[i] = int16_t(source[offset] | (uint16_t(source[(offset + 1) % dma2ItemSize]) << 8));
            }
            dma2PcmPending.insert(dma2PcmPending.end(), pcm.begin(), pcm.end());
        }
    }
    dma2EmittedBytes = end;
    const size_t ready = complete ? (dma2PcmPending.size() & ~size_t(1))
                                  : (dma2PcmPending.size() / 256 * 256);
    if (audioCallback && ready) {
        audioCallback(dma2PcmPending.data(), ready);
        dma2PcmPending.erase(dma2PcmPending.begin(), dma2PcmPending.begin() + ready);
    }
    if (complete) dma2PcmPending.clear();
    scheduleDma2Audio();
}

void Bus::setButtonMask(u32 newMask) {
    u32 changed = newMask ^ buttonMask;
    buttonMask = newMask;
    if (changed == 0) return;

    // External Interrupts 0..3 (GPF0..3)
    if (changed & (1 << 4)) requestIrq(0); // BTN_A -> EINT0
    if (changed & (1 << 5)) requestIrq(1); // BTN_B -> EINT1
    if (changed & (1 << 0)) requestIrq(2); // BTN_UP -> EINT2
    if (changed & (1 << 2)) requestIrq(3); // BTN_LEFT -> EINT3

    // External Interrupts 4..7 (GPF4..7)
    u32 eintMask = mmioRegs[0x560000A4];
    bool trig4_7 = false;
    if (changed & (1 << 3)) { mmioRegs[0x560000A8] |= (1 << 6); trig4_7 = true; } // BTN_RIGHT -> EINT6
    if (changed & (1 << 1)) { mmioRegs[0x560000A8] |= (1 << 7); trig4_7 = true; } // BTN_DOWN -> EINT7
    if (trig4_7 && ((mmioRegs[0x560000A8] & ~eintMask) & 0xF0)) {
        requestIrq(4); // EINT4_7
    }

    // External Interrupts 8..23 (GPG0..15 -> EINT8..23)
    bool trig8_23 = false;
    if (changed & (1 << 7)) { mmioRegs[0x560000A8] |= (1 << 16); trig8_23 = true; } // BTN_R -> GPG8 -> EINT16
    if (changed & (1 << 9)) { mmioRegs[0x560000A8] |= (1 << 17); trig8_23 = true; } // BTN_SELECT -> GPG9 -> EINT17
    if (changed & (1 << 8)) { mmioRegs[0x560000A8] |= (1 << 18); trig8_23 = true; } // BTN_START -> GPG10 -> EINT18
    if (changed & (1 << 6)) { mmioRegs[0x560000A8] |= (1 << 19); trig8_23 = true; } // BTN_L -> GPG11 -> EINT19
    if (trig8_23 && ((mmioRegs[0x560000A8] & ~eintMask) & 0x00FFFF00)) {
        requestIrq(5); // EINT8_23
    }
}

void Bus::tickDma2() {
    streamDma2Audio(true);
    dma2Active = false;
    dma2Timer = 0;
    mmioRegs[0x4B000094] = 0; // DSTAT2: CurTC = 0
    mmioRegs[0x4B000098] = dma2Src + (dma2SrcFixed ? 0 : dma2Count);
    mmioRegs[0x4B00009C] = dma2Dst + (dma2DstFixed ? 0 : dma2Count);

    // DCON/DISRC are reload registers. Linux queues a second buffer
    // while the first runs and expects it to load before the IRQ.
    if (!(mmioRegs[0x4B000090] & (1u << 22)) &&
        (mmioRegs[0x4B0000A0] & (1u << 1))) {
        writeMmio(0x4B0000A0, mmioRegs[0x4B0000A0]);
    } else {
        mmioRegs[0x4B0000A0] &= ~(1u << 1);
    }

    // Assert INT_DMA2 (bit 19 of SRCPND)
    requestIrq(19);
}

void Bus::tickAdcI2c(size_t cycles) {
    // S3C2410 ADC Conversion Handling
    if (adcPending) {
        if (cycles >= adcTimer) {
            adcPending = false;
            adcTimer = 0;
            mmioRegs[0x58000000] &= ~1;            // Clear ENABLE_START
            mmioRegs[0x58000000] |= (1 << 15);      // Set ECFLG (conversion complete)
            mmioRegs[0x5800000C] = 750;            // ADCDAT0 = 750
            mmioRegs[0x58000010] = 750;            // ADCDAT1 = 750

            // Trigger INT_ADC (SUBSRCPND bit 10)
            mmioRegs[0x4A000018] |= (1 << 10);

            // Propagate to main interrupt controller if not masked in INTSUBMSK
            u32 submsk = 0xFFFFFFFF;
            auto itSubMsk = mmioRegs.find(0x4A00001C);
            if (itSubMsk != mmioRegs.end()) submsk = itSubMsk->second;

            if ((submsk & (1 << 10)) == 0) {
                requestIrq(31);
            }
        } else {
            adcTimer -= cycles;
        }
    }

    // S3C2410 I2C Transfer Handling
    if (i2cPending) {
        if (cycles >= i2cTimer) {
            i2cPending = false;
            i2cTimer = 0;
            completeI2cByte();
        } else {
            i2cTimer -= cycles;
        }
    }
}

void Bus::completeI2cByte() {
    bool ack = false;
    if (i2cAddressPhase) {
        ack = eeprom.start(static_cast<u8>(mmioRegs[ADDR_IIC_BASE + 12]));
        i2cAddressPhase = false;
    } else if ((mmioRegs[ADDR_IIC_BASE + 4] & 0xc0) == 0x80) {
        mmioRegs[ADDR_IIC_BASE + 12] = eeprom.read();
        ack = (mmioRegs[ADDR_IIC_BASE] & 0x80) != 0;
    } else {
        ack = eeprom.write(static_cast<u8>(mmioRegs[ADDR_IIC_BASE + 12]));
    }
    mmioRegs[ADDR_IIC_BASE + 4] = (mmioRegs[ADDR_IIC_BASE + 4] & ~1u) | (ack ? 0u : 1u);
    mmioRegs[ADDR_IIC_BASE] |= 0x10;
    if (mmioRegs[ADDR_IIC_BASE] & 0x20) requestIrq(27);
    if (i2cLogging)
        std::cout << "[I2C COMPLETE] ack=" << ack << " data=0x" << std::hex
                  << mmioRegs[ADDR_IIC_BASE + 12] << std::dec << '\n';
}

void Bus::flushTlb() const {
    for (size_t i = 0; i < TLB_SIZE; ++i) {
        tlb[i].valid = false;
    }
}

u32 Bus::translateSlow(u32 va, MmuFault* fault, bool isWrite) const {
    if (fault) *fault = MmuFault::NONE;
    if (!mmuEnabled) return va;

    u32 vpn = va >> 12;
    u32 idx = vpn & (TLB_SIZE - 1);

    // Check TTB first-level translation table if mapped in SDRAM
    u32 ttbBase = ttb & ~0x3FFF;
    if (ttbBase >= ADDR_SDRAM_BASE && (ttbBase - ADDR_SDRAM_BASE) <= ADDR_SDRAM_SIZE - 16384) {
        u32 index = (va >> 20) & 0xFFF;
        u32 descOffset = ttbBase - ADDR_SDRAM_BASE + (index * 4);
        u32 desc = 0;
        std::memcpy(&desc, sdram.data() + descOffset, 4);

        if ((desc & 3) == 2) { // 1 MB Section Descriptor
            u32 ap = (desc >> 10) & 3;
            if (userMode) {
                if (ap == 0 || ap == 1 || (ap == 2 && isWrite)) {
                    if (fault) *fault = MmuFault::SECTION_PERMISSION_FAULT;
                    return 0xFFFFFFFF;
                }
            }
            u32 pa = (desc & 0xFFF00000) | (va & 0x000FFFFF);
            tlb[idx].vpn = vpn;
            tlb[idx].paBase = pa & ~0xFFF;
            tlb[idx].ap = ap;
            tlb[idx].valid = true;
            return pa;
        } else if ((desc & 3) == 1) { // Coarse Page Table
            u32 ptPhys = desc & ~0x3FF;
            if (ptPhys >= ADDR_SDRAM_BASE && (ptPhys - ADDR_SDRAM_BASE) <= ADDR_SDRAM_SIZE - 1024) {
                u32 ptIndex = (va >> 12) & 0xFF;
                u32 ptOffset = ptPhys - ADDR_SDRAM_BASE + (ptIndex * 4);
                u32 pte = 0;
                std::memcpy(&pte, sdram.data() + ptOffset, 4);
                if ((pte & 3) == 2 || (pte & 3) == 3) { // Small Page (4KB)
                    int apShift = 4 + (((va >> 10) & 3) * 2);
                    u32 ap = (pte >> apShift) & 3;
                    if (userMode) {
                        if (ap == 0 || ap == 1 || (ap == 2 && isWrite)) {
                            if (fault) *fault = MmuFault::PAGE_PERMISSION_FAULT;
                            return 0xFFFFFFFF;
                        }
                    }
                    u32 pa = (pte & 0xFFFFF000) | (va & 0xFFF);
                    tlb[idx].vpn = vpn;
                    tlb[idx].paBase = pa & ~0xFFF;
                    tlb[idx].ap = ap;
                    tlb[idx].valid = true;
                    return pa;
                } else if ((pte & 3) == 1) { // Large Page (64KB)
                    int apShift = 4 + (((va >> 14) & 3) * 2);
                    u32 ap = (pte >> apShift) & 3;
                    if (userMode) {
                        if (ap == 0 || ap == 1 || (ap == 2 && isWrite)) {
                            if (fault) *fault = MmuFault::PAGE_PERMISSION_FAULT;
                            return 0xFFFFFFFF;
                        }
                    }
                    u32 pa = (pte & 0xFFFF0000) | (va & 0xFFFF);
                    tlb[idx].vpn = vpn;
                    tlb[idx].paBase = pa & ~0xFFF;
                    tlb[idx].ap = ap;
                    tlb[idx].valid = true;
                    return pa;
                } else {
                    if (fault) *fault = MmuFault::PAGE_TRANSLATION_FAULT;
                    return 0xFFFFFFFF;
                }
            } else {
                if (fault) *fault = MmuFault::PAGE_TRANSLATION_FAULT;
                return 0xFFFFFFFF;
            }
        }
    }

    // Direct kernel linear SDRAM mapping (0xC0000000 -> 0x30000000)
    if (va >= 0xC0000000 && va < (0xC0000000 + ADDR_SDRAM_SIZE)) {
        u32 pa = (va - 0xC0000000) + ADDR_SDRAM_BASE;
        tlb[idx].vpn = vpn;
        tlb[idx].paBase = pa & ~0xFFF;
        tlb[idx].ap = 3;
        tlb[idx].valid = true;
        return pa;
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

    // High Exception Vector Page fallback (0xFFFF0000 .. 0xFFFF1000)
    if (va >= 0xFFFF0000 && va < 0xFFFF1000) {
        return 0x30001000 + (va - 0xFFFF0000);
    }

    if (fault) *fault = MmuFault::SECTION_TRANSLATION_FAULT;
    return 0xFFFFFFFF;
}

u32 Bus::getAudioSampleRate() const {
    auto itPsr = mmioRegs.find(0x55000008); // S3C2410 IISPSR
    if (itPsr == mmioRegs.end()) return 22050;
    u32 psr = itPsr->second;
    u32 psrA = (psr >> 5) & 0x1F;
    auto itMod = mmioRegs.find(0x55000004); // S3C2410 IISMOD
    u32 mod = (itMod != mmioRegs.end()) ? itMod->second : 0x99;
    u32 fsMul = (mod & (1 << 2)) ? 384 : 256;
    auto itCon = mmioRegs.find(0x55000000);
    // Unconfigured diagnostic fixtures retain their default prescaler behavior.
    const bool prescaler = itCon == mmioRegs.end() || (itCon->second & 2);
    u32 div = (prescaler ? psrA + 1 : 1) * fsMul;
    if (div == 0) return 22050;
    u32 rawRate = getPeripheralClock() / div;
    return (rawRate > 0) ? rawRate : 22050;
}

void Bus::updateClockedDevices() {
    timer4.setClock(getPeripheralClock());
    const auto con = mmioRegs.find(0x55000000), mod = mmioRegs.find(0x55000004);
    dma2Paused = (con != mmioRegs.end() && ((con->second & 0x21) != 0x21 || (con->second & 8))) ||
                 (mod != mmioRegs.end() && !(mod->second & 0x80));
    if (!dma2Active || !dma2Period) return;
    const uint64_t bytesPerSecond = uint64_t(getAudioSampleRate()) * 4;
    const size_t period = std::max<uint64_t>(1, uint64_t(dma2Count) * 20000000 / bytesPerSecond);
    if (period == dma2Period) return;
    // Preserve transfer progress when the source clock changes. Long double
    // avoids overflow for long transfers at a slow peripheral clock.
    dma2Timer = std::max<size_t>(1, size_t((static_cast<long double>(dma2Timer) / dma2Period) * period + 0.5L));
    dma2Period = period;
    scheduleDma2Audio();
}

void Bus::flushAudioClockBoundary() {
    const size_t ready = dma2PcmPending.size() & ~size_t(1);
    if (audioCallback && ready) {
        audioCallback(dma2PcmPending.data(), ready);
        dma2PcmPending.erase(dma2PcmPending.begin(), dma2PcmPending.begin() + ready);
    }
}

bool Bus::isLcd16Bpp() const {
    auto it = mmioRegs.find(0x4D000000); // S3C2410 LCDCON1
    if (it != mmioRegs.end()) {
        u32 lcdcon1 = it->second;
        u32 bppMode = (lcdcon1 >> 1) & 0x0F;
        if (bppMode == 12) return true; // TFT 16bpp (0b1100 = 64K color mode)
        if (bppMode == 4 || bppMode == 8) return false; // STN 12bpp / TFT 12bpp (0b0100)
    }
    auto itS1 = mmioRegs.find(0x4D000014); // LCDSADDR1
    auto itS2 = mmioRegs.find(0x4D000018); // LCDSADDR2
    if (itS1 != mmioRegs.end() && itS2 != mmioRegs.end() && itS1->second != 0 && itS2->second != 0) {
        u32 size = ((itS2->second & 0x1FFFFFFF) << 1) - ((itS1->second & 0x1FFFFFFF) << 1);
        if (size >= 76800) return true;
        if (size <= 57600) return false;
    }
    return isMmuEnabled(); // In Linux, default to 16bpp framebuffer unless programmed
}

size_t Bus::getFramebufferStride() const {
    const auto it = mmioRegs.find(0x4D00001C);
    const size_t minimum = isLcd16Bpp() ? 480 : 360;
    if (it == mmioRegs.end()) return minimum;
    const size_t stride = ((it->second & 0x7ff) + ((it->second >> 11) & 0x7ff)) * 2;
    return stride >= minimum ? stride : minimum;
}

unsigned Bus::getFramebufferHeight() const {
    const auto it = mmioRegs.find(0x4D000004); // LCDCON2 LINEVAL
    if (it == mmioRegs.end() || it->second == 0) return 160;
    return ((it->second >> 14) & 0x3ff) + 1;
}

u32 Bus::getFramebufferSize() const {
    return static_cast<u32>(getFramebufferStride() * getFramebufferHeight());
}

} // namespace oceanblast
