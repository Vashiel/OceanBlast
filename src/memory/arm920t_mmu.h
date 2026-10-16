#pragma once
#include "address_map.h"
#include <array>
#include <cstring>

namespace oceanblast {

// Translation state and table walking use a physical RAM view only. Device
// access and instruction-address caching remain responsibilities of the bus.
class Arm920TMmu {
public:
    enum class MmuFault {
        NONE = 0,
        SECTION_TRANSLATION_FAULT = 0x5,
        PAGE_TRANSLATION_FAULT = 0x7,
        SECTION_DOMAIN_FAULT = 0x9,
        PAGE_DOMAIN_FAULT = 0xB,
        SECTION_PERMISSION_FAULT = 0xD,
        PAGE_PERMISSION_FAULT = 0xF,
    };

    void setUserMode(bool value) { userMode = value; }
    bool isUserMode() const { return userMode; }
    bool isEnabled() const { return mmuEnabled; }
    void setEnabled(bool value) { mmuEnabled = value; flushTlb(); }
    void setTtb(u32 value) { ttb = value; flushTlb(); }
    u32 getTtb() const { return ttb; }
    void setDacr(u32 value) { dacr = value; }
    void flushTlbEntry(u32 va) const { tlb[(va >> 12) & (TLB_SIZE - 1)].valid = false; }
    void flushTlb() const {
        if (tlbEmpty) return;
        for (auto& entry : tlb) entry.valid = false;
        tlbEmpty = true;
    }
    u32 translateSlow(u32 va, const u8* ram, MmuFault* fault, bool isWrite) const;

    inline u32 translate(u32 va, const u8* ram, MmuFault* fault = nullptr, bool isWrite = false) const {
        if (__builtin_expect(!mmuEnabled, 0)) {
            if (fault) *fault = MmuFault::NONE;
            return va;
        }
        u32 vpn = va >> 12;
        u32 idx = vpn & (TLB_SIZE - 1);
        const TlbEntry& entry = tlb[idx];
        if (__builtin_expect(entry.valid && entry.vpn == vpn, 1)) {
            if (userMode) {
                const u32 ap = (entry.accessPermissions >> (((va >> 10) & 3) * 2)) & 3;
                if (__builtin_expect(ap <= (isWrite ? 2u : 1u), 0)) {
                    if (fault) *fault = entry.section ? MmuFault::SECTION_PERMISSION_FAULT : MmuFault::PAGE_PERMISSION_FAULT;
                    return 0xFFFFFFFF;
                }
            }
            if (fault) *fault = MmuFault::NONE;
            return entry.paBase | (va & 0xFFF);
        }
        return translateSlow(va, ram, fault, isWrite);
    }

private:
    // S3C2410 / ARM920T MMU State
    bool mmuEnabled = false;
    bool userMode = false;
    u32  ttb = 0;
    u32  dacr = 0;

    struct TlbEntry {
        u32 vpn = 0xFFFFFFFF;
        u32 paBase = 0;
        u8 accessPermissions = 0; // Four 1-KB subregion AP fields.
        bool section = false;
        bool valid = false;
    };
    static constexpr size_t TLB_SIZE = 2048;
    mutable std::array<TlbEntry, TLB_SIZE> tlb = {};
    mutable bool tlbEmpty = true;
};

inline u32 Arm920TMmu::translateSlow(u32 va, const u8* ram, MmuFault* fault, bool isWrite) const {
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
        std::memcpy(&desc, ram + descOffset, 4);

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
            tlb[idx].accessPermissions = static_cast<u8>(ap * 0x55);
            tlb[idx].section = true;
            tlb[idx].valid = true;
            tlbEmpty = false;
            return pa;
        } else if ((desc & 3) == 1) { // Coarse Page Table
            u32 ptPhys = desc & ~0x3FF;
            if (ptPhys >= ADDR_SDRAM_BASE && (ptPhys - ADDR_SDRAM_BASE) <= ADDR_SDRAM_SIZE - 1024) {
                u32 ptIndex = (va >> 12) & 0xFF;
                u32 ptOffset = ptPhys - ADDR_SDRAM_BASE + (ptIndex * 4);
                u32 pte = 0;
                std::memcpy(&pte, ram + ptOffset, 4);
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
                    tlb[idx].accessPermissions = static_cast<u8>((pte >> 4) & 0xff);
                    tlb[idx].section = false;
                    tlb[idx].valid = true;
                    tlbEmpty = false;
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
                    tlb[idx].accessPermissions = static_cast<u8>(ap * 0x55);
                    tlb[idx].section = false;
                    tlb[idx].valid = true;
                    tlbEmpty = false;
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
        tlb[idx].accessPermissions = 0xff;
        tlb[idx].section = false;
        tlb[idx].valid = true;
        tlbEmpty = false;
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

} // namespace oceanblast
