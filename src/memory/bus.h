#pragma once
#include "../core/types.h"
#include "arm920t_mmu.h"
#include "i2c_eeprom.h"
#include "timer4.h"
#include "clock_tree.h"
#include "usb_ohci.h"
#include "../display/frame_latch.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <array>
#include <cstring>
#include <algorithm>
#include <map>

namespace oceanblast {

class Bus {
public:
    Bus();
    ~Bus();

    void reset();
    void setI2cLogging(bool enabled) { i2cLogging = enabled; }
    void setMmioProfiling(bool enabled) { mmioProfiling = enabled; }
    bool saveMmioProfile(const std::string& path) const;
    struct MmioAccesses { uint64_t reads = 0, writes = 0; };
    const std::map<u32, MmioAccesses>& getMmioProfile() const { return mmioAccesses; }
    struct IrqDiagnostics {
        std::array<uint64_t, 32> requests{}, alreadyPending{}, sourceClears{}, selectedClears{};
    };
    const IrqDiagnostics& getIrqDiagnostics() const { return irqDiagnostics; }
    uint64_t getTimer4Expirations() const { return timer4.getExpirations(); }
    bool loadEeprom(const std::string& path);
    bool attachUsbStorage(const std::string& path) { usbHostEnabled = usbHost.attach(path); return usbHostEnabled; }
    bool saveEeprom(const std::string& path) const;
    void setEepromByte(size_t offset, u8 value) { eeprom.setByte(offset, value); }
    u8 getEepromByte(size_t offset) const { return eeprom.getByte(offset); }

    void enableHostFrameCapture(bool enabled, bool nativeVideo = false) { hostFrameEnabled = enabled; hostNativeVideo = nativeVideo; refreshHostFrameCapture(); }
    const FrameLatch& getHostFrameCapture() const { return hostFrameCapture; }
    bool hostFrameCaptureActive() const { return hostFrameSize != 0; }
    void refreshHostFrameCapture();
    inline void captureFrameWrite(u32 pa, unsigned bytes) {
        const u32 offset = pa - hostFrameBase;
        if (offset < hostFrameSize) hostFrameCapture.store(offset, bytes, sdramPtr + hostFrameBase - ADDR_SDRAM_BASE);
    }

    // MMU / Virtual Memory Translation
    using MmuFault = Arm920TMmu::MmuFault;

    void setUserMode(bool um) { if (mmu.isUserMode() != um) instructionPage = UINT32_MAX; mmu.setUserMode(um); }
    bool isUserMode() const { return mmu.isUserMode(); }

    void flushTlb() const;
    void flushTlbEntry(u32 va) const { instructionPage = UINT32_MAX; mmu.flushTlbEntry(va); }
    void setMmuEnabled(bool en) { instructionPage = UINT32_MAX; mmu.setEnabled(en); }
    void setTtb(u32 val) { instructionPage = UINT32_MAX; mmu.setTtb(val); }
    u32 getTtb() const { return mmu.getTtb(); }
    void setDacr(u32 val) { instructionPage = UINT32_MAX; mmu.setDacr(val); }
    bool isMmuEnabled() const { return mmu.isEnabled(); }

    u32 translateSlow(u32 va, MmuFault* fault, bool isWrite) const {
        return mmu.translateSlow(va, sdramPtr, fault, isWrite);
    }
    u32 translate(u32 va, MmuFault* fault = nullptr, bool isWrite = false) const {
        return mmu.translate(va, sdramPtr, fault, isWrite);
    }

    // Physical (translated) memory access slow paths
    u8   read8PhysSlow(u32 addr);
    u16  read16PhysSlow(u32 addr);
    u32  read32PhysSlow(u32 addr);
    void write8PhysSlow(u32 addr, u8 val);
    void write16PhysSlow(u32 addr, u16 val);
    void write32PhysSlow(u32 addr, u32 val);

    inline u8 read8Phys(u32 pa) {
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE, 1)) {
            return sdramPtr[pa - ADDR_SDRAM_BASE];
        }
        return read8PhysSlow(pa);
    }

    inline u16 read16Phys(u32 pa) {
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 2) && (pa & 1) == 0, 1)) {
            u16 val;
            std::memcpy(&val, sdramPtr + (pa - ADDR_SDRAM_BASE), 2);
            return val;
        }
        return read16PhysSlow(pa);
    }

    inline u32 read32Phys(u32 pa) {
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0, 1)) {
            u32 val;
            std::memcpy(&val, sdramPtr + (pa - ADDR_SDRAM_BASE), 4);
            return val;
        }
        return read32PhysSlow(pa);
    }

    // Cache a translated RAM address, never an instruction's contents. The
    // 1 KB span stays within a small-page access-permission subregion. Mode
    // changes and all guest TLB invalidations discard the cached address.
    inline u32 readInstruction(u32 va, bool thumb, MmuFault& fault) {
        const u32 mask = thumb ? ~1022u : ~1020u;
        if (__builtin_expect(((va ^ instructionPage) & mask) != 0, 0)) {
            const bool aligned = (va & (thumb ? 1u : 3u)) == 0;
            const u32 pa = translate(va, &fault);
            if (fault != MmuFault::NONE) return 0;
            if (!aligned || pa < ADDR_SDRAM_BASE || pa - ADDR_SDRAM_BASE >= ADDR_SDRAM_SIZE)
                return thumb ? read16Phys(pa) : read32Phys(pa);
            instructionPointer = sdramPtr + ((pa - ADDR_SDRAM_BASE) & ~1023u);
            instructionPage = va & ~1023u;
        }
        fault = MmuFault::NONE;
        if (thumb) {
            u16 value; std::memcpy(&value, instructionPointer + (va & 1023), sizeof(value)); return value;
        }
        u32 value; std::memcpy(&value, instructionPointer + (va & 1023), sizeof(value)); return value;
    }

    inline void write8Phys(u32 pa, u8 val) {
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE, 1)) {
            sdramPtr[pa - ADDR_SDRAM_BASE] = val;
            captureFrameWrite(pa, 1);
            return;
        }
        write8PhysSlow(pa, val);
    }

    inline u8 read8(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        return read8Phys(pa);
    }

    inline u16 read16(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        return read16Phys(pa);
    }

    inline u32 read32(u32 addr) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return 0;
        }
        return read32Phys(pa);
    }

    inline void write8(u32 addr, u8 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE, 1)) {
            sdramPtr[pa - ADDR_SDRAM_BASE] = val;
            captureFrameWrite(pa, 1);
            return;
        }
        write8PhysSlow(pa, val);
    }

    inline void write16(u32 addr, u16 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 2) && (pa & 1) == 0, 1)) {
            std::memcpy(sdramPtr + (pa - ADDR_SDRAM_BASE), &val, 2);
            captureFrameWrite(pa, 2);
            return;
        }
        write16PhysSlow(pa, val);
    }

    inline void write32(u32 addr, u32 val) {
        MmuFault fault = MmuFault::NONE;
        u32 pa = translate(addr, &fault, true);
        if (__builtin_expect(fault != MmuFault::NONE, 0)) {
            lastFault = fault;
            lastFaultAddr = addr;
            return;
        }
        if (__builtin_expect((pa - ADDR_SDRAM_BASE) <= (ADDR_SDRAM_SIZE - 4) && (pa & 3) == 0, 1)) {
            std::memcpy(sdramPtr + (pa - ADDR_SDRAM_BASE), &val, 4);
            captureFrameWrite(pa, 4);
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
    void setWadeTitleBlit(bool enabled) { wadeTitleBlit = enabled; }
    bool wadeTitleBlitEnabled() const { return wadeTitleBlit; }
    void tickDma2();
    void tickAdcI2c(size_t cycles);
    void updateUart0TxInterrupt(bool emptyTransition = false);

    inline bool hasLevelInterruptSource() const {
        return uart0TxLevelActive || usbHostEnabled;
    }
    // A zero-time tick may reassert these sources after acknowledgement.
    inline bool needsLevelInterruptRefresh() const {
        return (uart0TxLevelActive && !(regSrcpnd & (1u << 28))) ||
               (usbHostEnabled && usbHost.irq() && !(regSrcpnd & (1u << 26)));
    }

    inline void tick(size_t cycles = 1) {
        if (__builtin_expect(!cycles, 0)) {
            if (uart0TxLevelActive && !(regSrcpnd & (1u << 28))) updateUart0TxInterrupt();
            if (usbHostEnabled && usbHost.irq() && !(regSrcpnd & (1u << 26))) requestIrq(26);
            return;
        }
        if (uart0TxLevelActive && !(regSrcpnd & (1u << 28))) updateUart0TxInterrupt();
        if (usbHostEnabled && usbHost.irq() && !(regSrcpnd & (1u << 26))) requestIrq(26);
        if (usbHostEnabled && usbHost.needsTicks()) {
            usbHost.advance(cycles, sdramPtr, ADDR_SDRAM_SIZE, ADDR_SDRAM_BASE);
            if (usbHost.irq() && !(regSrcpnd & (1u << 26))) requestIrq(26);
        }
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

    // Bound idle advancement by the first modeled event, including PCM capture.
    uint64_t ticksUntilEvent() const {
        uint64_t next = std::min<uint64_t>(20000, timer4.ticksUntilExpiry());
        if (usbHostEnabled && usbHost.needsTicks()) next = std::min<uint64_t>(next, usbHost.ticksUntilEvent());
        if (dma2Active && (!dma2Paused || dma2Dst != 0x55000010)) {
            next = std::min<uint64_t>(next, dma2Timer);
            if (audioCallback && dma2Timer > dma2NextAudioTimer)
                next = std::min<uint64_t>(next, dma2Timer - dma2NextAudioTimer);
        }
        if (adcPending) next = std::min<uint64_t>(next, adcTimer);
        if (i2cPending) next = std::min<uint64_t>(next, i2cTimer);
        return std::max<uint64_t>(next, 1);
    }

    // MMIO State Inspection
    u32 getMmio(u32 addr) { return readMmio(addr, false); }

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
    unsigned getFramebufferHeight() const;
    u32  getFramebufferSize() const;

private:
    UsbOhci usbHost;
    bool usbHostEnabled = false;
    bool hostFrameEnabled = false, hostNativeVideo = false;
    u32 hostFrameBase = 0, hostFrameSize = 0;
    FrameLatch hostFrameCapture;
    bool mmioProfiling = false;
    IrqDiagnostics irqDiagnostics;
    std::map<u32, MmioAccesses> mmioAccesses;
    bool i2cLogging = false;
    // S3C2410 Keypad / GPIO Button State
    u32  buttonMask = 0;
    void requestIrq(u32 bit);
    void updateExtInterrupts();
    Arm920TMmu mmu;
    mutable MmuFault lastFault = MmuFault::NONE;
    mutable u32 lastFaultAddr = 0;
    mutable u32 instructionPage = UINT32_MAX;
    const u8* instructionPointer = nullptr;

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
    bool wadeTitleBlit = false;
    size_t dma2NextAudioTimer = 0;
    void streamDma2Audio(bool complete);
    void scheduleDma2Audio();
    void selectPendingIrq();
    AudioCallback audioCallback = nullptr;

    // S3C2410 MMIO Register Storage
    std::unordered_map<u32, u32> mmioRegs;

    u32  readMmio(u32 addr, bool guestAccess = true);
    void writeMmio(u32 addr, u32 val);

    u8   readNandByte();
    void writeNandCmd(u8 cmd);
    void writeNandAddr(u8 addr);
};

} // namespace oceanblast
