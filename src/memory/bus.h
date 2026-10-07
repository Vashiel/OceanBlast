#pragma once
#include "../core/types.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace oceanblast {

// S3C2410 Memory Map
constexpr u32 ADDR_STEPPINGSTONE_BASE = 0x00000000;
constexpr u32 ADDR_STEPPINGSTONE_SIZE = 4096; // 4 KB internal boot SRAM

constexpr u32 ADDR_SDRAM_BASE         = 0x30000000;
constexpr u32 ADDR_SDRAM_SIZE         = 32 * 1024 * 1024; // 32 MB SDRAM

// S3C2410 MMIO Peripheral Bases
constexpr u32 ADDR_MEMCON_BASE        = 0x48000000;
constexpr u32 ADDR_USBHOST_BASE       = 0x49000000;
constexpr u32 ADDR_INTCON_BASE        = 0x4A000000;
constexpr u32 ADDR_DMA_BASE           = 0x4B000000;
constexpr u32 ADDR_CLKCON_BASE        = 0x4C000000;
constexpr u32 ADDR_LCDCON_BASE        = 0x4D000000;
constexpr u32 ADDR_NANDCON_BASE       = 0x4E000000;
constexpr u32 ADDR_UART_BASE          = 0x50000000;
constexpr u32 ADDR_TIMER_BASE         = 0x51000000;
constexpr u32 ADDR_USBD_BASE          = 0x52000000;
constexpr u32 ADDR_WDT_BASE           = 0x53000000;
constexpr u32 ADDR_IIC_BASE           = 0x54000000;
constexpr u32 ADDR_IIS_BASE           = 0x55000000;
constexpr u32 ADDR_GPIO_BASE          = 0x56000000;
constexpr u32 ADDR_RTC_BASE           = 0x57000000;
constexpr u32 ADDR_ADC_BASE           = 0x58000000;
constexpr u32 ADDR_SPI_BASE           = 0x59000000;
constexpr u32 ADDR_SDI_BASE           = 0x5A000000;

class Bus {
public:
    Bus();
    ~Bus();

    void reset();

    // Memory Access
    u8  read8(u32 addr);
    u16 read16(u32 addr);
    u32 read32(u32 addr);

    void write8(u32 addr, u8 val);
    void write16(u32 addr, u16 val);
    void write32(u32 addr, u32 val);

    // Physical (translated) memory access
    u8   read8Phys(u32 addr);
    void write8Phys(u32 addr, u8 val);
    bool peek8(u32 va, u8& val) const;
    bool peek32(u32 va, u32& val) const;

    // Cartridge loading
    bool loadCartridge(const std::string& path);
    bool hasCartridge() const { return !cartNand.empty(); }
    const std::vector<u8>& getCartNand() const { return cartNand; }
    size_t getCartNandSize() const { return cartNand.size(); }
    bool isRawNand528() const { return rawNand528; }

    // Direct memory inspection
    const u8* getSteppingstonePtr() const { return steppingstone.data(); }
    const u8* getSdramPtr() const { return sdram.data(); }

    // MMU / Virtual Memory Translation
    enum class MmuFault {
        NONE = 0,
        SECTION_TRANSLATION_FAULT = 0x5,
        PAGE_TRANSLATION_FAULT = 0x7,
        SECTION_DOMAIN_FAULT = 0x9,
        PAGE_DOMAIN_FAULT = 0xB,
        SECTION_PERMISSION_FAULT = 0xD,
        PAGE_PERMISSION_FAULT = 0xF,
    };

    void setUserMode(bool um) { userMode = um; }
    bool isUserMode() const { return userMode; }

    void setMmuEnabled(bool en) { mmuEnabled = en; }
    void setTtb(u32 val) { ttb = val; }
    u32  getTtb() const { return ttb; }
    void setDacr(u32 val) { dacr = val; }
    bool isMmuEnabled() const { return mmuEnabled; }
    u32  translate(u32 va, MmuFault* fault = nullptr, bool isWrite = false) const;

    MmuFault getLastFault() const { return lastFault; }
    u32      getLastFaultAddr() const { return lastFaultAddr; }
    void     clearLastFault() const { lastFault = MmuFault::NONE; lastFaultAddr = 0; }

    // S3C2410 Interrupts & Timers
    bool hasPendingIrq() const;
    void tick(size_t cycles = 1);

private:
    // S3C2410 / ARM920T MMU State
    bool mmuEnabled = false;
    bool userMode = false;
    u32  ttb = 0;
    u32  dacr = 0;
    mutable MmuFault lastFault = MmuFault::NONE;
    mutable u32      lastFaultAddr = 0;
    std::vector<u8> steppingstone;
    std::vector<u8> sdram;
    std::vector<u8> cartNand;
    bool rawNand528 = true; // True if dump contains 16-byte OOB per 512-byte page

    // S3C2410 NAND Flash Controller State
    u32 nfconf = 0;
    u8  nfcmd  = 0;
    u8  nfstat = 0x1; // Bit 0: RnB ready
    int nandAddrCycle = 0;
    u32 nandColAddr = 0;
    u32 nandPageAddr = 0;
    u32 nandByteOffset = 0;
    bool nandReadActive = false;
    bool nandReadSpare = false;

    // S3C2410 ADC Controller State
    bool adcPending = false;
    size_t adcTimer = 0;

    // S3C2410 I2C Controller State
    bool i2cPending = false;
    size_t i2cTimer = 0;

    // S3C2410 PWM Timer 4 State
    u16 timer4Cnt = 0xFFFF;
    size_t timer4CycleCounter = 0;

    // S3C2410 MMIO Register Storage
    std::unordered_map<u32, u32> mmioRegs;

    u32  readMmio(u32 addr);
    void writeMmio(u32 addr, u32 val);

    u8   readNandByte();
    void writeNandCmd(u8 cmd);
    void writeNandAddr(u8 addr);
};

} // namespace oceanblast
