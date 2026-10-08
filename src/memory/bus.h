#pragma once
#include "../core/types.h"
#include "i2c_eeprom.h"
#include "timer4.h"
#include "clock_tree.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <array>
#include <cstring>

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
    void setI2cLogging(bool enabled) { i2cLogging = enabled; }
    bool loadEeprom(const std::string& path);
    bool saveEeprom(const std::string& path) const;

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

    void flushTlb() const;
    void setMmuEnabled(bool en) { mmuEnabled = en; flushTlb(); }
    void setTtb(u32 val) { ttb = val; flushTlb(); }
    u32  getTtb() const { return ttb; }
    void setDacr(u32 val) { dacr = val; }
    bool isMmuEnabled() const { return mmuEnabled; }

    u32  translateSlow(u32 va, MmuFault* fault, bool isWrite) const;

    inline u32 translate(u32 va, MmuFault* fault = nullptr, bool isWrite = false) const {
        if (!mmuEnabled) {
            if (fault) *fault = MmuFault::NONE;
            return va;
        }
        u32 vpn = va >> 12;
        u32 idx = vpn & (TLB_SIZE - 1);
        const TlbEntry& entry = tlb[idx];
        if (entry.valid && entry.vpn == vpn) {
            if (userMode) {
                u32 ap = entry.ap;
                if (ap == 0 || ap == 1 || (ap == 2 && isWrite)) {
                    if (fault) *fault = MmuFault::PAGE_PERMISSION_FAULT;
                    return 0xFFFFFFFF;
                }
            }
            if (fault) *fault = MmuFault::NONE;
            return entry.paBase | (va & 0xFFF);
        }
        return translateSlow(va, fault, isWrite);
    }

    // Physical (translated) memory access slow paths
    u8   read8PhysSlow(u32 addr);
    u16  read16PhysSlow(u32 addr);
    u32  read32PhysSlow(u32 addr);
    void write8PhysSlow(u32 addr, u8 val);
    void write16PhysSlow(u32 addr, u16 val);
    void write32PhysSlow(u32 addr, u32 val);

    inline u8 read8Phys(u32 pa) {
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE) {
            return sdramPtr[pa - ADDR_SDRAM_BASE];
        }
        return read8PhysSlow(pa);
    }

    inline u16 read16Phys(u32 pa) {
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 2) && (pa & 1) == 0) {
            u16 val;
            std::memcpy(&val, sdramPtr + (pa - ADDR_SDRAM_BASE), 2);
            return val;
        }
        return read16PhysSlow(pa);
    }

    inline u32 read32Phys(u32 pa) {
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0) {
            u32 val;
            std::memcpy(&val, sdramPtr + (pa - ADDR_SDRAM_BASE), 4);
            return val;
        }
        return read32PhysSlow(pa);
    }

    inline void write8Phys(u32 pa, u8 val) {
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE) {
            sdramPtr[pa - ADDR_SDRAM_BASE] = val;
            return;
        }
        write8PhysSlow(pa, val);
    }

    inline u8 read8(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        lastFault = MmuFault::NONE;
        return read8Phys(pa);
    }

    inline u16 read16(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        lastFault = MmuFault::NONE;
        return read16Phys(pa);
    }

    inline u32 read32(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        lastFault = MmuFault::NONE;
        return read32Phys(pa);
    }

    inline void write8(u32 addr, u8 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        lastFault = MmuFault::NONE;
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE) {
            sdramPtr[pa - ADDR_SDRAM_BASE] = val;
            return;
        }
        write8PhysSlow(pa, val);
    }

    inline void write16(u32 addr, u16 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        lastFault = MmuFault::NONE;
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 2) && (pa & 1) == 0) {
            std::memcpy(sdramPtr + (pa - ADDR_SDRAM_BASE), &val, 2);
            return;
        }
        write16PhysSlow(pa, val);
    }

    inline void write32(u32 addr, u32 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (fault != MmuFault::NONE) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        lastFault = MmuFault::NONE;
        if (pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0) {
            std::memcpy(sdramPtr + (pa - ADDR_SDRAM_BASE), &val, 4);
            return;
        }
        write32PhysSlow(pa, val);
    }

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

    MmuFault getLastFault() const { return lastFault; }
    u32      getLastFaultAddr() const { return lastFaultAddr; }
    void     clearLastFault() const { lastFault = MmuFault::NONE; lastFaultAddr = 0; }

    // S3C2410 Interrupts & Timers
    bool hasPendingIrq() const { return regIntpnd != 0; }
    void tickDma2();
    void tickAdcI2c(size_t cycles);
    void updateUart0TxInterrupt(bool emptyTransition = false);

    inline void tick(size_t cycles = 1) {
        // An already latched source needs no repeated MMIO-map lookup or write.
        // Register writes handle subpending/mask changes; tick reasserts after
        // the guest clears the main source while the empty level remains active.
        if (uart0TxLevelActive && !(regSrcpnd & (1u << 28))) updateUart0TxInterrupt();
        if (timer4.isRunning() && timer4.advance(cycles)) requestIrq(14);
        if (dma2Active && (!dma2Paused || dma2Dst != 0x55000010)) {
            if (cycles >= dma2Timer) {
                tickDma2();
            } else {
                dma2Timer -= cycles;
                if (audioCallback && dma2Timer <= dma2NextAudioTimer) streamDma2Audio(false);
            }
        }
        if (adcPending || i2cPending) {
            tickAdcI2c(cycles);
        }
    }

    // MMIO State Inspection
    u32 getMmio(u32 addr) { return readMmio(addr); }

    // Keypad / Button Input Subsystem
    void setButtonMask(u32 mask);
    u32  getButtonMask() const { return buttonMask; }

    // Audio / DMA Channel 2 Subsystem
    using AudioCallback = std::function<void(const int16_t* samples, size_t sampleCount)>;
    void setAudioCallback(AudioCallback cb) { audioCallback = cb; }
    u32  getAudioSampleRate() const;
    u32 getCpuClock() const { return clocks.fclk(); }
    u32 getBusClock() const { return clocks.hclk(); }
    u32 getPeripheralClock() const { return clocks.pclk(); }
    uint64_t getDma2RedundantEnables() const { return dma2RedundantEnables; }

    // S3C2410 LCD Subsystem
    bool isLcd16Bpp() const;
    size_t getFramebufferStride() const;
    u32  getFramebufferSize() const;

private:
    bool i2cLogging = false;
    // S3C2410 Keypad / GPIO Button State
    u32  buttonMask = 0;
    void requestIrq(u32 bit);
    // S3C2410 / ARM920T MMU State
    bool mmuEnabled = false;
    bool userMode = false;
    u32  ttb = 0;
    u32  dacr = 0;
    mutable MmuFault lastFault = MmuFault::NONE;
    mutable u32      lastFaultAddr = 0;

    struct TlbEntry {
        u32 vpn = 0xFFFFFFFF;
        u32 paBase = 0;
        u8  ap = 0;
        bool valid = false;
    };
    static constexpr size_t TLB_SIZE = 2048;
    mutable std::array<TlbEntry, TLB_SIZE> tlb = {};

    u32 regTcon = 0;
    u32 regIntmsk = ~0u;
    u32 regSrcpnd = 0;
    bool uart0TxLevelActive = false;
    u32 regIntpnd = 0;

    std::vector<u8> steppingstone;
    std::vector<u8> sdram;
    u8* sdramPtr = nullptr;
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
    bool i2cAddressPhase = false;
    I2cEeprom eeprom;
    void completeI2cByte();

    // S3C2410 PWM Timer 4 State
    Timer4 timer4;
    ClockTree clocks;
    void updateClockedDevices();
    void flushAudioClockBoundary();

    // S3C2410 DMA Channel 2 (IIS Audio) State
    bool   dma2Active = false;
    bool dma2Paused = false;
    size_t dma2Timer = 0;
    u32    dma2Src = 0;
    u32    dma2Count = 0;
    size_t dma2Period = 0;
    u32 dma2Dst = 0, dma2ItemSize = 1;
    bool dma2SrcFixed = false, dma2DstFixed = false;
    u32 dma2EmittedBytes = 0;
    std::vector<int16_t> dma2PcmPending;
    uint64_t dma2RedundantEnables = 0;
    size_t dma2NextAudioTimer = 0;
    void streamDma2Audio(bool complete);
    void scheduleDma2Audio();
    void selectPendingIrq();
    AudioCallback audioCallback = nullptr;

    // S3C2410 MMIO Register Storage
    std::unordered_map<u32, u32> mmioRegs;

    u32  readMmio(u32 addr);
    void writeMmio(u32 addr, u32 val);

    u8   readNandByte();
    void writeNandCmd(u8 cmd);
    void writeNandAddr(u8 addr);
};

} // namespace oceanblast
