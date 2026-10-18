#include "arm920t.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <sstream>

namespace oceanblast {

// ARMv4T word loads rotate an aligned word; they do not join bytes from two words.
static u32 readRotatedWord(Bus& bus, u32 address) {
    const u32 word = bus.read32(address & ~3u);
    const u32 shift = (address & 3u) * 8;
    return shift ? (word >> shift) | (word << (32 - shift)) : word;
}

ARM920T::ARM920T(Bus& bus) : bus(bus), ownedState(std::make_unique<ARM920TState>()), state(*ownedState) {
    reset();
}

ARM920T::ARM920T(Bus& bus, ARM920TState& sharedState) : bus(bus), state(sharedState) {}

void ARM920T::enterExecution(bool registerClocks) {
    cycleTiming = registerClocks;
    bus.setMmuEnabled((state.cp15_control & 1u) != 0);
    bus.setTtb(state.cp15_ttb);
    bus.setDacr(state.cp15_dacr);
    bus.setUserMode((state.cpsr & 0x1fu) == 0x10u);
    bus.flushTlb();
}

ARM920T::~ARM920T() {}

void ARM920T::completeCheckedDataAbort() {
    lastCycles=1;
    handleDataAbort(bus.getLastFaultAddr(),bus.getLastFault());
    bus.clearLastFault();
}

void ARM920T::reset(u32 startAddress) {
    acceleratedBlitStall = 0;
    lastEvent = StepEvent::Instruction;
    std::memset(state.r, 0, sizeof(state.r));
    for (auto& reg : state.fpa) {
        reg = {};
    }
    state.fpaFpsr = 0;
    state.r[15] = startAddress; // Reset vector (typically 0x00000000 in Steppingstone SRAM)
    state.r[13] = 0x00000F00;   // Boot stack pointer in Steppingstone SRAM
    state.cpsr  = 0x000000D3;   // Supervisor (SVC32) mode, ARM state, IRQ/FIQ disabled
    state.spsr  = 0;
    state.halted = false;
    state.waitingForInterrupt = false; lastCycles = 1;

    state.r13_usr = state.r14_usr = 0;
    state.r13_svc = state.r[13]; state.r14_svc = 0; state.spsr_svc = 0;
    state.r13_irq = state.r14_irq = state.spsr_irq = 0;
    state.r13_abt = state.r14_abt = state.spsr_abt = 0;
    state.r13_und = state.r14_und = state.spsr_und = 0;

    state.cp15_control = 0x00000070;
    state.cp15_ttb     = 0;
    state.cp15_dacr    = 0;
    bus.setMmuEnabled(false);
    bus.setTtb(0);
    bus.setDacr(0);
    bus.setUserMode(false);
}

void ARM920T::switchMode(u32 newMode) {
    u32 oldMode = state.cpsr & 0x1F;
    if (newMode == oldMode) return;

    // 1. Save state of current mode
    switch (oldMode) {
        case 0x10: // USR
        case 0x1F: // SYS
            state.r13_usr = state.r[13];
            state.r14_usr = state.r[14];
            break;
        case 0x13: // SVC
            state.r13_svc = state.r[13];
            state.r14_svc = state.r[14];
            state.spsr_svc = state.spsr;
            break;
        case 0x12: // IRQ
            state.r13_irq = state.r[13];
            state.r14_irq = state.r[14];
            state.spsr_irq = state.spsr;
            break;
        case 0x17: // ABT
            state.r13_abt = state.r[13];
            state.r14_abt = state.r[14];
            state.spsr_abt = state.spsr;
            break;
        case 0x1B: // UND
            state.r13_und = state.r[13];
            state.r14_und = state.r[14];
            state.spsr_und = state.spsr;
            break;
        default:
            break;
    }

    // 2. Restore state of new mode
    switch (newMode) {
        case 0x10: // USR
        case 0x1F: // SYS
            state.r[13] = state.r13_usr;
            state.r[14] = state.r14_usr;
            break;
        case 0x13: // SVC
            state.r[13] = state.r13_svc;
            state.r[14] = state.r14_svc;
            state.spsr = state.spsr_svc;
            break;
        case 0x12: // IRQ
            state.r[13] = state.r13_irq;
            state.r[14] = state.r14_irq;
            state.spsr = state.spsr_irq;
            break;
        case 0x17: // ABT
            state.r[13] = state.r13_abt;
            state.r[14] = state.r14_abt;
            state.spsr = state.spsr_abt;
            break;
        case 0x1B: // UND
            state.r[13] = state.r13_und;
            state.r[14] = state.r14_und;
            state.spsr = state.spsr_und;
            break;
        default:
            break;
    }

    state.cpsr = (state.cpsr & ~0x1F) | (newMode & 0x1F);
    bus.setUserMode((state.cpsr & 0x1F) == 0x10);
}

void ARM920T::handleIrq() {
    lastEvent = StepEvent::Interrupt;
    u32 oldCpsr = state.cpsr;
    switchMode(0x12); // IRQ mode
    state.spsr = oldCpsr;
    state.r[14] = state.r[15] + 4; // Return address for `sub lr, lr, #4`
    state.cpsr |= FLAG_I;   // Disable further IRQs
    state.cpsr &= ~FLAG_T;  // ARM state
    state.r[15] = (state.cp15_control & (1 << 13)) ? 0xFFFF0018 : 0x00000018;
}

void ARM920T::handlePrefetchAbort(u32 faultPC) {
    lastEvent = StepEvent::PrefetchAbort;
    if (faultLogging) logFaultContext("prefetch", faultPC, faultPC);
    u32 retAddr = faultPC + 4;
    u32 oldCpsr = state.cpsr;
    switchMode(0x17); // Abort mode
    state.spsr = oldCpsr;
    state.cpsr |= FLAG_I;   // Disable IRQ
    state.cpsr &= ~FLAG_T;  // ARM state
    state.r[14] = retAddr;  // r14_abt = faultPC + 4
    state.r[15] = (state.cp15_control & (1 << 13)) ? 0xFFFF000C : 0x0000000C;

    static int pabtCount = 0;
    if (pabtCount++ < 30 && debugLogging) {
        std::cout << "[PREFETCH ABORT #" << pabtCount << "] faultPC=0x" << std::hex << faultPC
                  << " -> vector 0x" << state.r[15] << " retAddr=0x" << retAddr << std::dec << std::endl;
    }
}

void ARM920T::handleDataAbort(u32 faultAddr, Bus::MmuFault faultType) {
    lastEvent = StepEvent::DataAbort;
    if (faultLogging) logFaultContext("data", state.r[15] - (isThumb() ? 2 : 4), faultAddr);
    state.cp15_far = faultAddr;
    state.cp15_fsr = static_cast<u32>(faultType);
    u32 retAddr = (state.cpsr & FLAG_T) ? (state.r[15] + 6) : (state.r[15] + 4); // instruction_pc + 8
    u32 oldCpsr = state.cpsr;
    switchMode(0x17); // Abort mode
    state.spsr = oldCpsr;
    state.cpsr |= FLAG_I;   // Disable IRQ
    state.cpsr &= ~FLAG_T;  // ARM state
    state.r[14] = retAddr;  // r14_abt = instruction_pc + 8
    state.r[15] = (state.cp15_control & (1 << 13)) ? 0xFFFF0010 : 0x00000010;

    static int dabtCount = 0;
    if (dabtCount++ < 50 && debugLogging) {
        std::cout << "[DATA ABORT #" << dabtCount << "] faultAddr=0x" << std::hex << faultAddr
                  << " fsr=0x" << state.cp15_fsr << " -> vector 0x" << state.r[15]
                  << " retAddr=0x" << retAddr << std::dec << std::endl;
    }
}

void ARM920T::handleUndefinedInstruction(u32 instr) {
    lastEvent = StepEvent::UndefinedInstruction;
    if (faultLogging) logFaultContext("undefined", state.r[15] - (isThumb() ? 2 : 4), instr);
    u32 retAddr = state.r[15]; // address after the undefined instruction
    u32 oldCpsr = state.cpsr;
    switchMode(0x1B); // UND mode
    state.spsr = oldCpsr;
    state.cpsr |= FLAG_I;   // Disable IRQ
    state.cpsr &= ~FLAG_T;  // ARM state
    state.r[14] = retAddr;  // r14_und = instruction_pc + 4
    state.r[15] = (state.cp15_control & (1 << 13)) ? 0xFFFF0004 : 0x00000004;

    static int undCount = 0;
    if (undCount++ < 15 && debugLogging) {
        std::cout << "[UNDEF INSTR #" << undCount << "] instr=0x" << std::hex << instr
                  << " at 0x" << (retAddr - 4) << " -> vector 0x" << state.r[15] << std::dec << std::endl;
    }
}

void ARM920T::logFaultContext(const char* kind, u32 instructionPC, u32 faultAddress) {
    std::cout << "[FAULT CONTEXT] kind=" << kind << " PC=0x" << std::hex << instructionPC
              << " address=0x" << faultAddress << " TTB=0x" << bus.getTtb() << std::dec << '\n';
    dumpState();
    for (int offset = -16; offset <= 16; offset += 4) {
        const u32 address = instructionPC + offset;
        u32 word = 0;
        if (bus.peek32(address & ~3u, word))
            std::cout << "  code[0x" << std::hex << (address & ~3u) << "]=0x" << word << '\n';
    }
    std::cout << std::dec;
}

void ARM920T::dumpState() const {
    std::cout << "[CPU] PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << state.r[15]
              << " SP: 0x" << std::setw(8) << state.r[13]
              << " LR: 0x" << std::setw(8) << state.r[14]
              << " CPSR: 0x" << std::setw(8) << state.cpsr
              << (isThumb() ? " (Thumb)" : " (ARM)") << std::dec << std::endl;
    std::cout << "  r12: 0x" << std::hex << std::setw(8) << state.r[12] << std::dec << '\n';
    for (int i = 0; i < 12; i += 4) {
        std::cout << "  r" << i << ": 0x" << std::hex << std::setw(8) << state.r[i]
                  << "  r" << std::dec << (i+1) << ": 0x" << std::hex << std::setw(8) << state.r[i+1]
                  << "  r" << std::dec << (i+2) << ": 0x" << std::hex << std::setw(8) << state.r[i+2]
                  << "  r" << std::dec << (i+3) << ": 0x" << std::hex << std::setw(8) << state.r[i+3] << std::dec << std::endl;
    }
}

// Immediate ALU operations that preserve flags need no carry or shifter state.
// The caller has selected unconditional ARM instruction class 1.
static inline bool executeImmediateNoFlags(ARM920TState& state,u32 instr) {
    if (instr & (1u<<20)) return false;
    const u32 opcode=(instr>>21)&15, rn=(instr>>16)&15, rd=(instr>>12)&15;
    if (rn==15 || rd==15 || (opcode>=5 && opcode<=11)) return false;
    const u32 rotation=((instr>>8)&15)*2, value=instr&255;
    const u32 right=rotation?(value>>rotation)|(value<<(32-rotation)):value;
    const u32 left=state.r[rn];
    u32 result;
    switch (opcode) {
        case 0: result=left&right; break;
        case 1: result=left^right; break;
        case 2: result=left-right; break;
        case 3: result=right-left; break;
        case 4: result=left+right; break;
        case 12: result=left|right; break;
        case 13: result=right; break;
        case 14: result=left&~right; break;
        case 15: result=~right; break;
        default: return false;
    }
    state.r[rd]=result;
    return true;
}

// Register ALU operations that preserve flags and use no register-controlled
// shift need no shifter carry or NZCV update.
static inline bool executeRegisterNoFlags(ARM920TState& state,u32 instr) {
    const u32 opcode=(instr>>21)&15, rn=(instr>>16)&15, rd=(instr>>12)&15, rm=instr&15;
    if (rn==15 || rd==15 || rm==15 || (opcode>=5 && opcode<=11)) return false;
    u32 right=state.r[rm];
    const u32 shift=(instr>>4)&255;
    if (shift) {
        const u32 amount=shift>>3;
        switch ((shift>>1)&3) {
            case 0: right<<=amount; break;
            case 1: right=amount?(right>>amount):0u; break;
            case 2: right=amount?u32(i32(right)>>amount):u32(i32(right)>>31); break;
            case 3:
                right=amount?((right>>amount)|(right<<(32-amount)))
                            :(((state.cpsr&FLAG_C)?0x80000000u:0u)|(right>>1));
                break;
        }
    }
    const u32 left=state.r[rn];
    u32 result;
    switch (opcode) {
        case 0: result=left&right; break;
        case 1: result=left^right; break;
        case 2: result=left-right; break;
        case 3: result=right-left; break;
        case 4: result=left+right; break;
        case 12: result=left|right; break;
        case 13: result=right; break;
        case 14: result=left&~right; break;
        case 15: result=~right; break;
        default: return false;
    }
    state.r[rd]=result;
    return true;
}

namespace {
u32 blitChannel(u32 pixel, u32 mask, u32 shift, u32 loss) {
    if (!mask) return 0;
    return ((pixel & mask) >> shift) << loss;
}
}

// Wade's title spends the opening inside this SDL soft blit while IIS DMA is
// already running, so the mixer writes a bad first stretch. Doing the blit on
// the host and keeping a quarter of its guest time lets the mixer catch up
// without raising the fight ratio or interpreting the pixel loop.
bool ARM920T::accelerateTitleBlit(size_t peripheralTicks) {
    const u32 info = state.r[0];
    const u32 src = bus.read32(info + 0);
    const int width = int(bus.read32(info + 4));
    const int height = int(bus.read32(info + 8));
    const u32 sskip = bus.read32(info + 12);
    const u32 dst = bus.read32(info + 16);
    const u32 dskip = bus.read32(info + 28);
    const u32 srcFmt = bus.read32(info + 36);
    const u32 dstFmt = bus.read32(info + 44);
    if (!src || !dst || !srcFmt || !dstFmt) return false;
    if (width < 1 || height < 1 || width > 720 || height > 480) return false;
    const u32 sbpp = bus.read8(srcFmt + 5);
    const u32 dbpp = bus.read8(dstFmt + 5);
    if (sbpp < 1 || sbpp > 4 || dbpp < 1 || dbpp > 4) return false;
    const u32 sRmask = bus.read32(srcFmt + 16), sGmask = bus.read32(srcFmt + 20), sBmask = bus.read32(srcFmt + 24);
    const u32 dRmask = bus.read32(dstFmt + 16), dGmask = bus.read32(dstFmt + 20), dBmask = bus.read32(dstFmt + 24);
    const u32 sRshift = bus.read8(srcFmt + 10), sGshift = bus.read8(srcFmt + 11), sBshift = bus.read8(srcFmt + 12);
    const u32 dRshift = bus.read8(dstFmt + 10), dGshift = bus.read8(dstFmt + 11), dBshift = bus.read8(dstFmt + 12);
    const u32 sRloss = bus.read8(srcFmt + 6), sGloss = bus.read8(srcFmt + 7), sBloss = bus.read8(srcFmt + 8);
    const u32 dRloss = bus.read8(dstFmt + 6), dGloss = bus.read8(dstFmt + 7), dBloss = bus.read8(dstFmt + 8);
    const u32 alpha = bus.read8(srcFmt + 36);
    struct Page { u32 base = 0xffffffffu; u8* ptr = nullptr; bool write = false; } srcPage, dstPage;
    auto byte = [&](Page& page, u32 va, bool write) -> u8* {
        const u32 base = va & ~1023u;
        if (page.base != base || page.write != write) {
            Bus::MmuFault fault = Bus::MmuFault::NONE;
            const u32 pa = bus.translate(va, &fault, write);
            if (fault != Bus::MmuFault::NONE || pa < ADDR_SDRAM_BASE || pa >= ADDR_SDRAM_BASE + ADDR_SDRAM_SIZE) return nullptr;
            if ((pa & 1023u) != (va & 1023u)) return nullptr;
            page.ptr = const_cast<u8*>(bus.getSdramPtr()) + ((pa & ~1023u) - ADDR_SDRAM_BASE);
            page.base = base;
            page.write = write;
        }
        return page.ptr + (va & 1023u);
    };
    auto loadPixel = [&](Page& page, u32 va, u32 bpp) -> u32 {
        u32 pixel = 0;
        for (u32 b = 0; b < bpp; ++b) {
            u8* p = byte(page, va + b, false);
            pixel |= u32(p ? *p : bus.read8(va + b)) << (8 * b);
        }
        return pixel;
    };
    auto storePixel = [&](u32 va, u32 pixel) {
        for (u32 b = 0; b < dbpp; ++b) bus.write8(va + b, u8(pixel >> (8 * b)));
    };
    u32 sva = src, dva = dst;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const u32 pixel = loadPixel(srcPage, sva, sbpp);
            sva += sbpp;
            u32 r = blitChannel(pixel, sRmask, sRshift, sRloss);
            u32 g = blitChannel(pixel, sGmask, sGshift, sGloss);
            u32 b = blitChannel(pixel, sBmask, sBshift, sBloss);
            if (alpha < 255) {
                const u32 under = loadPixel(dstPage, dva, dbpp);
                const u32 keep = 256u - alpha;
                r = (r * alpha + blitChannel(under, dRmask, dRshift, dRloss) * keep) >> 8;
                g = (g * alpha + blitChannel(under, dGmask, dGshift, dGloss) * keep) >> 8;
                b = (b * alpha + blitChannel(under, dBmask, dBshift, dBloss) * keep) >> 8;
            }
            u32 out = 0;
            if (dRmask) out |= (r >> dRloss) << dRshift;
            if (dGmask) out |= (g >> dGloss) << dGshift;
            if (dBmask) out |= (b >> dBloss) << dBshift;
            storePixel(dva, out);
            dva += dbpp;
        }
        sva += sskip;
        dva += dskip;
    }
    state.r[15] = state.r[14] & ~1u;
    // About a quarter of a generic ARM blit. Empty steps keep DMA and frame
    // time in the range that still matches the soundtrack.
    acceleratedBlitStall = u32(width) * u32(height) * 4u;
    lastCycles = 1;
    bus.tick(peripheralTicks);
    return true;
}

void ARM920T::step(size_t peripheralTicks) {
    lastEvent = StepEvent::Instruction;
    lastCycles = 1;
    if (state.halted) { lastEvent = StepEvent::Halted; return; }
    if (acceleratedBlitStall) {
        lastEvent = StepEvent::LegacyAcceleration;
        --acceleratedBlitStall;
        bus.tick(peripheralTicks);
        return;
    }
    if (state.waitingForInterrupt) {
        if (!bus.hasPendingIrq()) { lastEvent = StepEvent::Waiting; lastCycles = 0; bus.tick(peripheralTicks); return; }
        state.waitingForInterrupt = false;
    }

    if (!(state.cpsr & FLAG_I) && bus.hasPendingIrq()) {
        handleIrq();
        lastCycles = 3;
        bus.tick(peripheralTicks);
        return;
    }

    if (!isThumb() && state.r[15] == 0x40110cc4u) {
        const u32 rate = bus.getAudioSampleRate();
        if (bus.wadeTitleBlitEnabled() && rate >= 20000 && rate <= 24000 && accelerateTitleBlit(peripheralTicks)) {
            lastEvent = StepEvent::LegacyAcceleration;
            return;
        }
    }

    u32 currentPC = state.r[15];
    Bus::MmuFault fetchFault = Bus::MmuFault::NONE;
    const bool thumb = isThumb();
    const u32 instruction = bus.readInstruction(currentPC, thumb, fetchFault);
    if (fetchFault != Bus::MmuFault::NONE) {
        handlePrefetchAbort(currentPC);
        lastCycles = 3;
        bus.tick(peripheralTicks);
        return;
    }

    if (thumb) {
        stepThumb(static_cast<u16>(instruction));
    } else if (simpleAluExecution && !debugLogging &&
               ((instruction & 0xfe000000u) == 0xe2000000u
                    ? executeImmediateNoFlags(state,instruction)
                    : ((instruction & 0xfe100010u) == 0xe0000000u &&
                       executeRegisterNoFlags(state,instruction)))) {
        state.r[15] += 4;
    } else {
        stepARM(instruction);
    }

    if (peripheralTicks || bus.hasLevelInterruptSource()) {
        bus.tick(peripheralTicks);
    }
}

bool ARM920T::evaluateCondition(u32 cond) const {
    static constexpr u16 kConditionMask[16] = {
        0xF0F0, // 0x0 EQ: Z == 1
        0x0F0F, // 0x1 NE: Z == 0
        0xCCCC, // 0x2 CS / HS: C == 1
        0x3333, // 0x3 CC / LO: C == 0
        0xFF00, // 0x4 MI: N == 1
        0x00FF, // 0x5 PL: N == 0
        0xAAAA, // 0x6 VS: V == 1
        0x5555, // 0x7 VC: V == 0
        0x0C0C, // 0x8 HI: C == 1 && Z == 0
        0xF3F3, // 0x9 LS: C == 0 || Z == 1
        0xAA55, // 0xA GE: N == V
        0x55AA, // 0xB LT: N != V
        0x0A05, // 0xC GT: Z == 0 && N == V
        0xF5FA, // 0xD LE: Z == 1 || N != V
        0xFFFF, // 0xE AL: Always
        0xFFFF, // 0xF NV: Reserved
    };
    return cond < 16 && ((kConditionMask[cond] >> (state.cpsr >> 28)) & 1u) != 0;
}

void ARM920T::setNZFlags(u32 result) {
    state.cpsr &= ~(FLAG_N | FLAG_Z);
    if (result & 0x80000000) state.cpsr |= FLAG_N;
    if (result == 0)          state.cpsr |= FLAG_Z;
}

void ARM920T::setAddFlags(u32 a, u32 b, u32 res) {
    setNZFlags(res);
    if (static_cast<u64>(a) + static_cast<u64>(b) > 0xFFFFFFFFULL) state.cpsr |= FLAG_C;
    else state.cpsr &= ~FLAG_C;
    if (((a ^ res) & (b ^ res) & 0x80000000) != 0) state.cpsr |= FLAG_V;
    else state.cpsr &= ~FLAG_V;
}

void ARM920T::setSubFlags(u32 a, u32 b, u32 res) {
    setNZFlags(res);
    if (a >= b) state.cpsr |= FLAG_C;
    else state.cpsr &= ~FLAG_C;
    if (((a ^ b) & (a ^ res) & 0x80000000) != 0) state.cpsr |= FLAG_V;
    else state.cpsr &= ~FLAG_V;
}

u32 ARM920T::shiftOperand(u32 val, u32 type, u32 amount, bool& carryOut, bool immediate) {
    carryOut = (state.cpsr & FLAG_C) != 0;
    if (immediate && amount == 0) {
        if (type == 1 || type == 2) amount = 32;
        else if (type == 3) {
            u32 result = (carryOut ? 0x80000000u : 0u) | (val >> 1);
            carryOut = (val & 1) != 0;
            return result;
        }
    }
    if (amount == 0) return val;

    switch (type) {
        case 0: // LSL
            if (amount < 32) {
                carryOut = (val >> (32 - amount)) & 1;
                return val << amount;
            } else if (amount == 32) {
                carryOut = val & 1;
                return 0;
            } else {
                carryOut = false;
                return 0;
            }
        case 1: // LSR
            if (amount < 32) {
                carryOut = (val >> (amount - 1)) & 1;
                return val >> amount;
            } else if (amount == 32) {
                carryOut = (val >> 31) & 1;
                return 0;
            } else {
                carryOut = false;
                return 0;
            }
        case 2: // ASR
            if (amount < 32) {
                carryOut = (static_cast<i32>(val) >> (amount - 1)) & 1;
                return static_cast<u32>(static_cast<i32>(val) >> amount);
            } else {
                carryOut = (val >> 31) & 1;
                return (val & 0x80000000) ? 0xFFFFFFFF : 0;
            }
        case 3: // ROR
            amount %= 32;
            if (amount == 0) { carryOut = (val >> 31) != 0; return val; }
            carryOut = (val >> (amount - 1)) & 1;
            return (val >> amount) | (val << (32 - amount));
        default:
            return val;
    }
}

// Immediate and constant-shift register ALU operations need no general decoder.
// Fetch, condition checks and peripheral/exception boundaries remain in step().
bool ARM920T::executeSimpleDataProcessing(u32 instr) {
    if ((instr & 0x0c000000) != 0 || ((instr >> 12) & 15) == 15) return false;
    const bool immediate = (instr & (1u << 25)) != 0;
    if (!immediate && (instr & (1u << 4))) return false;
    const u32 opcode = (instr >> 21) & 15;
    const bool flags = (instr & (1u << 20)) != 0;
    // Control-register encodings (TST/TEQ/CMP/CMN without S bit) use the general path.
    if (opcode >= 8 && opcode <= 11 && !flags) return false;
    const u32 rn = (instr >> 16) & 15;
    const u32 rd = (instr >> 12) & 15;
    const u32 op1 = rn == 15 ? state.r[15] + 4 : state.r[rn];
    u32 op2;
    bool carry = (state.cpsr & FLAG_C) != 0;
    const bool arithmeticCarry = carry;
    if (immediate) {
        const u32 rotation = ((instr >> 8) & 15) * 2;
        const u32 value = instr & 255;
        op2 = rotation ? (value >> rotation) | (value << (32 - rotation)) : value;
        if (rotation) carry = (op2 & FLAG_N) != 0;
    } else {
        const u32 rm = instr & 15;
        op2 = rm == 15 ? state.r[15] + 4 : state.r[rm];
        const u32 amount = (instr >> 7) & 31;
        switch ((instr >> 5) & 3) {
            case 0: // LSL #0 leaves the operand and carry unchanged.
                if (amount) { carry = (op2 >> (32 - amount)) & 1; op2 <<= amount; }
                break;
            case 1: // Encoded LSR #0 means LSR #32.
                carry = (op2 >> (amount ? amount - 1 : 31)) & 1;
                op2 = amount ? op2 >> amount : 0;
                break;
            case 2: // Encoded ASR #0 means ASR #32.
                carry = (op2 >> (amount ? amount - 1 : 31)) & 1;
                op2 = amount ? u32(int32_t(op2) >> amount) : u32(int32_t(op2) >> 31);
                break;
            case 3:
                if (amount) {
                    carry = (op2 >> (amount - 1)) & 1;
                    op2 = (op2 >> amount) | (op2 << (32 - amount));
                } else { // RRX uses the old carry before replacing it.
                    const u32 value = (carry ? FLAG_N : 0) | (op2 >> 1);
                    carry = (op2 & 1) != 0; op2 = value;
                }
                break;
        }
    }
    u32 result = 0;
    const u32 adcCarry = arithmeticCarry ? 1u : 0u;
    const u32 sbcBorrow = arithmeticCarry ? 0u : 1u;
    switch (opcode) {
        case 0: case 8: result = op1 & op2; break;
        case 1: case 9: result = op1 ^ op2; break;
        case 2: case 10: result = op1 - op2; break;
        case 3: result = op2 - op1; break;
        case 4: case 11: result = op1 + op2; break;
        case 5: result = op1 + op2 + adcCarry; break;
        case 6: result = op1 - op2 - sbcBorrow; break;
        case 7: result = op2 - op1 - sbcBorrow; break;
        case 12: result = op1 | op2; break;
        case 13: result = op2; break;
        case 14: result = op1 & ~op2; break;
        case 15: result = ~op2; break;
    }
    if (opcode < 8 || opcode > 11) state.r[rd] = result;
    if (flags) {
        if (opcode == 2 || opcode == 10) setSubFlags(op1, op2, result);
        else if (opcode == 3) setSubFlags(op2, op1, result);
        else if (opcode == 4 || opcode == 11) setAddFlags(op1, op2, result);
        else if (opcode == 5) {
            setNZFlags(result);
            if (static_cast<u64>(op1) + op2 + adcCarry > 0xFFFFFFFFULL) state.cpsr |= FLAG_C;
            else state.cpsr &= ~FLAG_C;
            if (((op1 ^ result) & (op2 ^ result) & 0x80000000u) != 0) state.cpsr |= FLAG_V;
            else state.cpsr &= ~FLAG_V;
        } else if (opcode == 6) {
            setNZFlags(result);
            if (static_cast<u64>(op1) >= static_cast<u64>(op2) + sbcBorrow) state.cpsr |= FLAG_C;
            else state.cpsr &= ~FLAG_C;
            if (((op1 ^ op2) & (op1 ^ result) & 0x80000000u) != 0) state.cpsr |= FLAG_V;
            else state.cpsr &= ~FLAG_V;
        } else if (opcode == 7) {
            setNZFlags(result);
            if (static_cast<u64>(op2) >= static_cast<u64>(op1) + sbcBorrow) state.cpsr |= FLAG_C;
            else state.cpsr &= ~FLAG_C;
            if (((op2 ^ op1) & (op2 ^ result) & 0x80000000u) != 0) state.cpsr |= FLAG_V;
            else state.cpsr &= ~FLAG_V;
        } else {
            setNZFlags(result);
            state.cpsr = (state.cpsr & ~FLAG_C) | (carry ? FLAG_C : 0);
        }
    }
    lastCycles = 1;
    return true;
}

void ARM920T::stepARM(u32 instr) {
    u32 pc = state.r[15];
    if (debugLogging) {
    if (pc == 0xc001a538) {
        static int irqDbgCount = 0;
        if (irqDbgCount++ < 10) {
            std::cout << "[IRQ_DISPATCH] r0=" << std::hex << state.r[0] << " r1=" << state.r[1]
                      << " [r1+0]=" << bus.read32(state.r[1])
                      << " [r1+4]=" << bus.read32(state.r[1]+4)
                      << " [r1+8]=" << bus.read32(state.r[1]+8) << std::dec << std::endl;
        }
    }
    if (pc == 0xc001a540) {
        static int irqCallCount = 0;
        if (irqCallCount++ < 10) {
            std::cout << "[IRQ_CALL] r3=" << std::hex << state.r[3] << " [r3]=" << bus.read32(state.r[3]) << std::dec << std::endl;
        }
    }
    if (pc == 0xc00198c8) {
        std::cout << "[RET_TO_USER] sp=" << std::hex << state.r[13]
                  << " [sp+0x3c]=" << bus.read32(state.r[13] + 0x3c)
                  << " [sp+0x40]=" << bus.read32(state.r[13] + 0x40)
                  << " cpsr=" << state.cpsr << std::dec << std::endl;
    }
    if (pc == 0xc00703ac) {
        std::cout << "[EXEC_MMAP] r9=0x" << std::hex << state.r[9] << " [r9+0x100]=0x" << bus.read32(state.r[9] + 0x100) << std::dec << std::endl;
    }
    if (pc == 0xc006ffc0) {
        std::cout << "[DO_EXECVE mm_alloc call]" << std::endl;
    }
    if (pc == 0xc006ffc4) {
        std::cout << "[DO_EXECVE mm_alloc ret] mm=0x" << std::hex << state.r[0]
                  << " pgd=0x" << bus.read32(state.r[0] + 0x1c) << std::dec << std::endl;
    }
    if (pc == 0xc0070420) {
        std::cout << "[PRE_SWITCH_MM] r4=0x" << std::hex << state.r[4]
                  << " [r4+0x1c]=0x" << bus.read32(state.r[4] + 0x1c)
                  << " r5=0x" << state.r[5] << " r6=0x" << state.r[6]
                  << " LR=0x" << state.r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0021be8) {
        std::cout << "[SWITCH_MM] new TTB=0x" << std::hex << state.r[0] << " lr=0x" << state.r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0125180) {
        std::cout << "[ADC PROBE CALLED] r0=0x" << std::hex << state.r[0] << " lr=0x" << state.r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0125020 || pc == 0xc0124f20) {
        std::cout << "[ADC LOCK] PC=0x" << std::hex << pc << " LR=0x" << state.r[14]
                  << " r0=0x" << state.r[0] << " sem_count=" << (i32)bus.read32(state.r[0])
                  << " CPSR=0x" << state.cpsr << std::dec << std::endl;
    }
    if (pc == 0xc0147660) {
        static int schedEntryCount = 0;
        std::cout << "[SCHEDULE_ENTRY #" << schedEntryCount << "] LR=0x" << std::hex << state.r[14]
                  << " SP=0x" << state.r[13] << " CPSR=0x" << state.cpsr << std::dec << std::endl;
        if (schedEntryCount == 52) {
            std::cout << "--- [STACK DUMP at SCHEDULE_ENTRY #52] ---" << std::hex << "\n";
            for (u32 s = state.r[13]; s < state.r[13] + 160; s += 4) {
                std::cout << "  [0x" << s << "] = 0x" << bus.read32(s) << "\n";
            }
            std::cout << std::dec;
        }
        schedEntryCount++;
    }
    if (pc == 0xc0147ae4) {
        char comm10[17] = {0};
        char comm8[17] = {0};
        for (int i = 0; i < 16; i++) {
            comm10[i] = bus.read8(state.r[10] + 0x1a4 + i);
            comm8[i] = bus.read8(state.r[8] + 0x1a4 + i);
        }
        std::cout << "[SCHEDULE_SWITCH] next='" << comm10 << "' (0x" << std::hex << state.r[10]
                  << ") prev='" << comm8 << "' (0x" << state.r[8] << ")" << std::dec << std::endl;
    }
    if (pc == 0xc0019844) {
        std::cout << "[SWITCH_TO] r1=" << std::hex << state.r[1] << " r2=" << state.r[2] << "\n";
        for (int i = 0; i <= 40; i += 4) {
            std::cout << "  [r2 + " << i << "] = 0x" << bus.read32(state.r[2] + i) << "\n";
        }
        std::cout << std::dec;
    }
    }
    state.r[15] += 4; // Advance PC to instruction address + 4

    u32 cond = instr >> 28;
    if (cond < 0xE && !evaluateCondition(cond)) return;

    // Select the major instruction class before examining overlapping
    // encodings. Loads and branches avoid the control/multiply comparisons.
    switch ((instr >> 25) & 7) {
        case 0:
            if ((instr & 0x90) == 0x90) {
                if (instr & 0x60) executeHalfwordTransfer(instr);
                else if (!(instr & (1u << 24))) executeMultiply(instr);
                else if ((instr & 0x0FB00FF0) == 0x01000090) executeSwap(instr);
                else executeDataProcessing(instr);
                return;
            }
            if (simpleAluExecution && executeSimpleDataProcessing(instr)) return;
            if ((instr & 0x01900000) == 0x01000000) {
                if ((instr & 0x0FFFFFF0) == 0x012FFF10 || (instr & 0x0FFFFFF0) == 0x012FFF30) executeBX(instr);
                else if ((instr & 0x0FBF0FFF) == 0x010F0000) executeMRS(instr);
                else if ((instr & 0x0FB0F000) == 0x0120F000) executeMSR(instr);
                else executeDataProcessing(instr);
            } else {
                executeDataProcessing(instr);
            }
            return;
        case 1:
            if (simpleAluExecution && executeSimpleDataProcessing(instr)) return;
            if ((instr & 0x0FB0F000) == 0x0320F000) executeMSR(instr);
            else executeDataProcessing(instr);
            return;
        case 2: case 3: executeSingleDataTransfer(instr); return;
        case 4: executeBlockDataTransfer(instr); return;
        case 5: executeBranch(instr); return;
        case 6: {
            const u32 cp = (instr >> 8) & 15;
            if (fpa11Execution && (cp == 1 || cp == 2) && executeFPA11DataTransfer(instr)) return;
            break;
        }
        case 7:
            if ((instr & 0x0F000000) == 0x0F000000) { executeSWI(instr); return; }
            if ((instr & 0x0F000010) == 0x0E000010 && ((instr >> 8) & 15) == 15) { executeCP15(instr); return; }
            if (fpa11Execution && ((instr >> 8) & 15) == 1 && executeFPA11Operation(instr)) return;
            break;
    }
    static int unkCount = 0;
    if (unkCount++ < 10 && debugLogging) {
        std::cerr << "[CPU] Undefined/Unhandled ARM instruction 0x" << std::hex << std::setw(8) << instr
                  << " at PC 0x" << pc << std::dec << std::endl;
    }
    handleUndefinedInstruction(instr);
}

static inline float fpaBitsToFloat(u32 w) {
    float f = 0.0f;
    std::memcpy(&f, &w, sizeof(f));
    return f;
}

static inline u32 fpaFloatToBits(float f) {
    u32 w = 0;
    std::memcpy(&w, &f, sizeof(w));
    return w;
}

static inline double fpaWordsToDouble(u32 lo, u32 hi) {
    const u64 bits = (static_cast<u64>(hi) << 32) | lo;
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

static inline void fpaDoubleToWords(double d, u32& lo, u32& hi) {
    u64 bits = 0;
    std::memcpy(&bits, &d, sizeof(bits));
    lo = static_cast<u32>(bits);
    hi = static_cast<u32>(bits >> 32);
}

float ARM920T::readFpaSingle(u32 reg) const {
    const FpaReg& fr = state.fpa[reg & 7u];
    if (fr.type == 1) return fpaBitsToFloat(fr.w0);
    if (fr.type == 2) return static_cast<float>(fpaWordsToDouble(fr.w0, fr.w1));
    return 0.0f;
}

double ARM920T::readFpaDouble(u32 reg) const {
    const FpaReg& fr = state.fpa[reg & 7u];
    if (fr.type == 1) return static_cast<double>(fpaBitsToFloat(fr.w0));
    if (fr.type == 2) return fpaWordsToDouble(fr.w0, fr.w1);
    return 0.0;
}

void ARM920T::writeFpaSingle(u32 reg, float val) {
    FpaReg& fr = state.fpa[reg & 7u];
    fr.type = 1;
    fr.w0 = fpaFloatToBits(val);
    fr.w1 = 0;
}

void ARM920T::writeFpaDouble(u32 reg, double val) {
    FpaReg& fr = state.fpa[reg & 7u];
    fr.type = 2;
    fpaDoubleToWords(val, fr.w0, fr.w1);
}

bool ARM920T::executeFPA11DataTransfer(u32 instr) {
    const u32 cp = (instr >> 8) & 15u;
    const u32 rn = (instr >> 16) & 15u;
    const u32 fd = (instr >> 12) & 7u;
    const u32 offset = (instr & 0xFFu) * 4u;
    const bool pre = (instr & (1u << 24)) != 0;
    const bool up = (instr & (1u << 23)) != 0;
    const bool writeback = (instr & (1u << 21)) != 0 && rn != 15u;
    const bool isLoad = (instr & (1u << 20)) != 0;
    const u32 baseVal = (rn == 15u) ? (state.r[15] + 4u) : state.r[rn];
    const u32 wbAddr = up ? (baseVal + offset) : (baseVal - offset);
    const u32 addr = pre ? wbAddr : baseVal;
    const u32 tr = ((instr >> 21) & 2u) | ((instr >> 15) & 1u);

    if (cp == 1u) {
        if (tr == 0u) { // Single (32-bit)
            lastCycles = 2;
            if (isLoad) {
                const u32 w = bus.read32(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
                state.fpa[fd] = {1, w, 0};
            } else {
                const u32 w = (state.fpa[fd].type == 2) ? fpaFloatToBits(static_cast<float>(readFpaDouble(fd))) : state.fpa[fd].w0;
                bus.write32(addr, w);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
            }
        } else if (tr == 1u) { // Double (64-bit, FPA mixed-endian word order: hi at addr, lo at addr + 4)
            lastCycles = 3;
            if (isLoad) {
                const u32 lo = bus.read32(addr + 4u);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
                const u32 hi = bus.read32(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
                state.fpa[fd] = {2, lo, hi};
            } else {
                u32 lo = state.fpa[fd].w0, hi = state.fpa[fd].w1;
                if (state.fpa[fd].type == 1) fpaDoubleToWords(static_cast<double>(fpaBitsToFloat(state.fpa[fd].w0)), lo, hi);
                else if (state.fpa[fd].type == 0) { lo = 0; hi = 0; }
                bus.write32(addr, hi);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
                bus.write32(addr + 4u, lo);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
            }
        } else {
            return false;
        }
        if (writeback) state.r[rn] = wbAddr;
        return true;
    }

    if (cp == 2u) { // LFM / SFM (3 words = 12 bytes per register)
        const u32 count = (tr == 0u) ? 4u : tr;
        lastCycles = 1u + count * 3u;
        if (isLoad) {
            FpaReg tmp[4]{};
            for (u32 i = 0; i < count; ++i) {
                const u32 a = addr + i * 12u;
                const u32 tag = bus.read32(a);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
                const u8 t = static_cast<u8>((tag >> 14) & 3u);
                u32 lo = 0, hi = 0;
                if (t == 1u || t == 2u) {
                    lo = bus.read32(a + 8u);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) {
                        handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                        bus.clearLastFault();
                        return true;
                    }
                    hi = bus.read32(a + 4u);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) {
                        handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                        bus.clearLastFault();
                        return true;
                    }
                }
                tmp[i] = {t, lo, hi};
            }
            for (u32 i = 0; i < count; ++i) state.fpa[(fd + i) & 7u] = tmp[i];
        } else {
            for (u32 i = 0; i < count; ++i) {
                const u32 reg = (fd + i) & 7u;
                const u32 a = addr + i * 12u;
                if (state.fpa[reg].type == 1u || state.fpa[reg].type == 2u) {
                    bus.write32(a + 8u, state.fpa[reg].w0);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) {
                        handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                        bus.clearLastFault();
                        return true;
                    }
                    bus.write32(a + 4u, state.fpa[reg].w1);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) {
                        handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                        bus.clearLastFault();
                        return true;
                    }
                }
                bus.write32(a, static_cast<u32>(state.fpa[reg].type) << 14);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return true;
                }
            }
        }
        if (writeback) state.r[rn] = wbAddr;
        return true;
    }
    return false;
}

bool ARM920T::executeFPA11Operation(u32 instr) {
    static constexpr double kFpaImm[8] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 0.5, 10.0};
    const u32 prec = ((instr >> 18) & 2u) | ((instr >> 7) & 1u);
    const u32 rnd = (instr >> 5) & 3u;
    lastCycles = 1;

    if (instr & 0x10u) { // CPRT cp1
        if (instr & 0x00800000u) { // CMF / CMNF / CMFE / CMNFE
            const u32 fn = (instr >> 16) & 7u;
            const u32 fm = instr & 7u;
            const double lhs = readFpaDouble(fn);
            double rhs = (instr & 8u) ? kFpaImm[fm] : readFpaDouble(fm);
            if (instr & 0x00200000u) rhs = -rhs;
            u32 flags = 0;
            if (std::isnan(lhs) || std::isnan(rhs)) {
                flags = (state.fpaFpsr & 0x1000u) ? (FLAG_C | FLAG_V) : FLAG_V;
            } else if (lhs == rhs) {
                flags = FLAG_Z | FLAG_C;
            } else if (lhs < rhs) {
                flags = FLAG_N;
            } else {
                flags = FLAG_C;
            }
            state.cpsr = (state.cpsr & 0x0FFFFFFFu) | flags;
            return true;
        }
        const u32 opc = (instr >> 20) & 7u;
        const u32 rd = (instr >> 12) & 15u;
        switch (opc) {
            case 0: { // FLT Fn, Rd
                const u32 fn = (instr >> 16) & 7u;
                const i32 val = static_cast<i32>((rd == 15u) ? (state.r[15] + 4u) : state.r[rd]);
                if (prec == 0u) writeFpaSingle(fn, static_cast<float>(val));
                else writeFpaDouble(fn, static_cast<double>(val));
                return true;
            }
            case 1: { // FIX Rd, Fm
                const u32 fm = instr & 7u;
                const double d = (instr & 8u) ? kFpaImm[fm] : readFpaDouble(fm);
                double rounded = d;
                switch (rnd) {
                    case 0: rounded = std::rint(d); break;
                    case 1: rounded = std::ceil(d); break;
                    case 2: rounded = std::floor(d); break;
                    case 3: rounded = std::trunc(d); break;
                }
                i32 out = 0;
                if (std::isnan(rounded)) out = 0;
                else if (rounded >= 2147483647.0) out = 2147483647;
                else if (rounded <= -2147483648.0) out = static_cast<i32>(0x80000000u);
                else out = static_cast<i32>(rounded);
                if (rd != 15u) state.r[rd] = static_cast<u32>(out);
                return true;
            }
            case 2: { // WFS Rd
                const u32 val = (rd == 15u) ? (state.r[15] + 4u) : state.r[rd];
                state.fpaFpsr = (state.fpaFpsr & 0xFF000000u) | (val & 0x00FFFFFFu);
                return true;
            }
            case 3: { // RFS Rd
                if (rd != 15u) state.r[rd] = state.fpaFpsr;
                return true;
            }
            case 4: // WFC
                return true;
            case 5: // RFC
                if (rd != 15u) state.r[rd] = 0;
                return true;
            default:
                return false;
        }
    }

    // CPDO cp1
    const u32 opc = ((instr >> 20) & 15u) | (((instr >> 15) & 1u) << 4);
    const u32 fd = (instr >> 12) & 7u;
    const u32 fn = (instr >> 16) & 7u;
    const u32 fm = instr & 7u;

    if (prec == 0u) { // Single precision
        const float lhs = readFpaSingle(fn);
        const float rhs = (instr & 8u) ? static_cast<float>(kFpaImm[fm]) : readFpaSingle(fm);
        float res = 0.0f;
        switch (opc) {
            case 0x00: res = lhs + rhs; break; // ADF
            case 0x01: case 0x09: res = lhs * rhs; break; // MUF / FML
            case 0x02: res = lhs - rhs; break; // SUF
            case 0x03: res = rhs - lhs; break; // RSF
            case 0x04: case 0x0A: res = lhs / rhs; break; // DVF / FDV
            case 0x05: case 0x0B: res = rhs / lhs; break; // RDF / FRD
            case 0x06: res = std::pow(lhs, rhs); break; // POW
            case 0x07: res = std::pow(rhs, lhs); break; // RPW
            case 0x08: res = std::remainder(lhs, rhs); break; // RMF
            case 0x10: case 0x1F: res = rhs; break; // MVF / NRM
            case 0x11: res = -rhs; break; // MNF
            case 0x12: res = std::fabs(rhs); break; // ABS
            case 0x13: case 0x1E: // RND / URD
                res = (rnd == 0u) ? std::rint(rhs) : (rnd == 1u) ? std::ceil(rhs) : (rnd == 2u) ? std::floor(rhs) : std::trunc(rhs);
                break;
            case 0x14: res = std::sqrt(rhs); break; // SQT
            case 0x15: res = std::log10(rhs); break; // LOG
            case 0x16: res = std::log(rhs); break; // LGN
            case 0x17: res = std::exp(rhs); break; // EXP
            case 0x18: res = std::sin(rhs); break; // SIN
            case 0x19: res = std::cos(rhs); break; // COS
            case 0x1A: res = std::tan(rhs); break; // TAN
            case 0x1B: res = std::asin(rhs); break; // ASN
            case 0x1C: res = std::acos(rhs); break; // ACS
            case 0x1D: res = std::atan(rhs); break; // ATN
            default: return false;
        }
        writeFpaSingle(fd, res);
        return true;
    }

    // Double / Extended precision
    const double lhs = readFpaDouble(fn);
    const double rhs = (instr & 8u) ? kFpaImm[fm] : readFpaDouble(fm);
    double res = 0.0;
    switch (opc) {
        case 0x00: res = lhs + rhs; break;
        case 0x01: case 0x09: res = lhs * rhs; break;
        case 0x02: res = lhs - rhs; break;
        case 0x03: res = rhs - lhs; break;
        case 0x04: case 0x0A: res = lhs / rhs; break;
        case 0x05: case 0x0B: res = rhs / lhs; break;
        case 0x06: res = std::pow(lhs, rhs); break;
        case 0x07: res = std::pow(rhs, lhs); break;
        case 0x08: res = std::remainder(lhs, rhs); break;
        case 0x10: case 0x1F: res = rhs; break;
        case 0x11: res = -rhs; break;
        case 0x12: res = std::fabs(rhs); break;
        case 0x13: case 0x1E:
            res = (rnd == 0u) ? std::rint(rhs) : (rnd == 1u) ? std::ceil(rhs) : (rnd == 2u) ? std::floor(rhs) : std::trunc(rhs);
            break;
        case 0x14: res = std::sqrt(rhs); break;
        case 0x15: res = std::log10(rhs); break;
        case 0x16: res = std::log(rhs); break;
        case 0x17: res = std::exp(rhs); break;
        case 0x18: res = std::sin(rhs); break;
        case 0x19: res = std::cos(rhs); break;
        case 0x1A: res = std::tan(rhs); break;
        case 0x1B: res = std::asin(rhs); break;
        case 0x1C: res = std::acos(rhs); break;
        case 0x1D: res = std::atan(rhs); break;
        default: return false;
    }
    writeFpaDouble(fd, res);
    return true;
}

void ARM920T::executeBranch(u32 instr) {
    lastCycles = 3;
    bool link = (instr & (1 << 24)) != 0;
    i32 offset = instr & 0x00FFFFFF;
    if (offset & 0x00800000) offset |= 0xFF000000;
    offset <<= 2;

    if (link) state.r[14] = state.r[15]; // Next instruction address (current PC + 4)
    state.r[15] = static_cast<u32>(static_cast<i32>(state.r[15] + 4) + offset);
}

void ARM920T::executeBX(u32 instr) {
    lastCycles = 3;
    bool link = (instr & (1 << 5)) != 0;
    u32 rm = instr & 0xF;
    u32 target = (rm == 15) ? (state.r[15] + 4) : state.r[rm];

    if (link) state.r[14] = state.r[15];

    if (target & 1) {
        state.cpsr |= FLAG_T;
        state.r[15] = target & ~1;
    } else {
        state.cpsr &= ~FLAG_T;
        state.r[15] = target & ~3;
    }
}

void ARM920T::executeMRS(u32 instr) {
    bool isSPSR = (instr & (1 << 22)) != 0;
    u32 rd = (instr >> 12) & 0xF;
    if (rd != 15) {
        state.r[rd] = isSPSR ? state.spsr : state.cpsr;
    }
}

void ARM920T::executeMSR(u32 instr) {
    bool isSPSR = (instr & (1 << 22)) != 0;
    u32 fields = (instr >> 16) & 0xF;
    u32 mask = 0;
    if (fields & 1) mask |= 0x000000FF; // c
    if (fields & 2) mask |= 0x0000FF00; // x
    if (fields & 4) mask |= 0x00FF0000; // s
    if (fields & 8) mask |= 0xFF000000; // f

    if (!isSPSR && (state.cpsr & 0x1F) == 0x10) { // User mode
        mask &= 0xFF000000; // Only condition flags (f) can be modified in User mode
    }

    u32 val = 0;
    if (instr & (1 << 25)) { // Immediate
        u32 imm = instr & 0xFF;
        u32 rot = ((instr >> 8) & 0xF) * 2;
        val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else { // Register
        u32 rm = instr & 0xF;
        val = (rm == 15) ? (state.r[15] + 4) : state.r[rm];
    }

    if (isSPSR) {
        state.spsr = (state.spsr & ~mask) | (val & mask);
    } else {
        u32 newCpsr = (state.cpsr & ~mask) | (val & mask);
        if ((mask & 0x1F) && (newCpsr & 0x1F) != (state.cpsr & 0x1F)) {
            switchMode(newCpsr & 0x1F);
        }
        state.cpsr = newCpsr;
    }
}

void ARM920T::executeCP15(u32 instr) {
    bool is_mrc = (instr & (1 << 20)) != 0;
    u32 crn = (instr >> 16) & 0xF;
    u32 rd  = (instr >> 12) & 0xF;

    if (is_mrc) {
        u32 val = 0;
        if (crn == 0) val = 0x41009200; // ARM920T (ARMv4T) ID Code
        else if (crn == 1) val = state.cp15_control;
        else if (crn == 2) val = state.cp15_ttb;
        else if (crn == 3) val = state.cp15_dacr;
        else if (crn == 5) val = state.cp15_fsr;
        else if (crn == 6) val = state.cp15_far;
        if (rd != 15) state.r[rd] = val;
    } else {
        u32 val = (rd == 15) ? (state.r[15] + 4) : state.r[rd];
        if (crn == 1) {
            state.cp15_control = val;
            bus.setMmuEnabled((val & 1) != 0);
        } else if (crn == 2) {
            if (debugLogging) std::cout << "[CP15 TTB WRITE] val=0x" << std::hex << val << " PC=0x" << state.r[15] << std::dec << std::endl;
            state.cp15_ttb = val;
            bus.setTtb(val);
        } else if (crn == 3) {
            state.cp15_dacr = val;
            bus.setDacr(val);
        } else if (crn == 5) {
            state.cp15_fsr = val;
        } else if (crn == 6) {
            state.cp15_far = val;
        } else if (crn == 8) {
            if (((instr >> 5) & 7) == 1) bus.flushTlbEntry(val);
            else bus.flushTlb();
        }
        if (cycleTiming && crn == 7 && (instr & 15) == 0 &&
            ((instr >> 5) & 7) == 4 && ((instr >> 21) & 7) == 0)
            state.waitingForInterrupt = true;
        // Cache maintenance remains a functional NOP.
    }
}

void ARM920T::executeDataProcessing(u32 instr) {
    bool isImm = (instr & (1 << 25)) != 0;
    u32 opcode = (instr >> 21) & 0xF;
    bool setCond = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;

    lastCycles = (!isImm && (instr & (1u << 4))) ? 2 : 1;
    if (rd == 15 && !(opcode >= 8 && opcode <= 11)) lastCycles += 2;
    u32 op1 = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
    u32 op2 = 0;
    bool carry = (state.cpsr & FLAG_C) != 0;
    const bool arithmeticCarry = carry;

    if (isImm) {
        u32 imm = instr & 0xFF;
        u32 rot = ((instr >> 8) & 0xF) * 2;
        if (rot == 0) {
            op2 = imm;
        } else {
            op2 = (imm >> rot) | (imm << (32 - rot));
            carry = (op2 >> 31) != 0;
        }
    } else {
        u32 rm = instr & 0xF;
        op2 = (rm == 15) ? (state.r[15] + 4) : state.r[rm];
        u32 shiftType = (instr >> 5) & 3;
        bool isRegShift = (instr & (1 << 4)) != 0;
        u32 shiftAmt = 0;
        if (isRegShift) {
            u32 rs = (instr >> 8) & 0xF;
            shiftAmt = state.r[rs] & 0xFF;
        } else {
            shiftAmt = (instr >> 7) & 0x1F;
        }
        if (isRegShift || shiftType != 0 || shiftAmt != 0)
            op2 = shiftOperand(op2, shiftType, shiftAmt, carry, !isRegShift);
    }

    u32 result = 0;
    bool writeResult = true;

    switch (opcode) {
        case 0x0: result = op1 & op2; break; // AND
        case 0x1: result = op1 ^ op2; break; // EOR
        case 0x2: result = op1 - op2; if (setCond) setSubFlags(op1, op2, result); break; // SUB
        case 0x3: result = op2 - op1; if (setCond) setSubFlags(op2, op1, result); break; // RSB
        case 0x4: result = op1 + op2; if (setCond) setAddFlags(op1, op2, result); break; // ADD
        case 0x5: { // ADC
            u32 cVal = arithmeticCarry ? 1 : 0;
            result = op1 + op2 + cVal;
            if (setCond) {
                setNZFlags(result);
                if (static_cast<u64>(op1) + op2 + cVal > 0xFFFFFFFFULL) state.cpsr |= FLAG_C;
                else state.cpsr &= ~FLAG_C;
                if (((op1 ^ result) & (op2 ^ result) & 0x80000000) != 0) state.cpsr |= FLAG_V;
                else state.cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x6: { // SBC
            u32 cVal = arithmeticCarry ? 0 : 1;
            result = op1 - op2 - cVal;
            if (setCond) {
                setNZFlags(result);
                if (static_cast<u64>(op1) >= static_cast<u64>(op2) + cVal) state.cpsr |= FLAG_C;
                else state.cpsr &= ~FLAG_C;
                if (((op1 ^ op2) & (op1 ^ result) & 0x80000000) != 0) state.cpsr |= FLAG_V;
                else state.cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x7: { // RSC
            u32 cVal = arithmeticCarry ? 0 : 1;
            result = op2 - op1 - cVal;
            if (setCond) {
                setNZFlags(result);
                if (static_cast<u64>(op2) >= static_cast<u64>(op1) + cVal) state.cpsr |= FLAG_C;
                else state.cpsr &= ~FLAG_C;
                if (((op2 ^ op1) & (op2 ^ result) & 0x80000000) != 0) state.cpsr |= FLAG_V;
                else state.cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x8: // TST
        case 0x9: // TEQ
            result = opcode == 0x8 ? (op1 & op2) : (op1 ^ op2);
            writeResult = false;
            if (setCond) {
                setNZFlags(result);
                if (carry) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
            }
            break;
        case 0xA: result = op1 - op2; writeResult = false; if (setCond) setSubFlags(op1, op2, result); break; // CMP
        case 0xB: result = op1 + op2; writeResult = false; if (setCond) setAddFlags(op1, op2, result); break; // CMN
        case 0xC: result = op1 | op2; break; // ORR
        case 0xD: result = op2; break;       // MOV
        case 0xE: result = op1 & ~op2; break; // BIC
        case 0xF: result = ~op2; break;      // MVN
    }

    if (writeResult) {
        if (rd == 15) {
            state.r[15] = result & ~3;
            if (setCond) {
                u32 newCpsr = state.spsr;
                if ((newCpsr & 0x1F) != (state.cpsr & 0x1F)) {
                    switchMode(newCpsr & 0x1F);
                }
                state.cpsr = newCpsr;
            }
        } else {
            state.r[rd] = result;
            if (setCond && opcode != 0x2 && opcode != 0x3 && opcode != 0x4 && opcode != 0x5 && opcode != 0x6 && opcode != 0x7) {
                setNZFlags(result);
                if (carry) state.cpsr |= FLAG_C;
                else state.cpsr &= ~FLAG_C;
            }
        }
    }
}

void ARM920T::executeSingleDataTransfer(u32 instr) {
    u32 rd = (instr >> 12) & 0xF;
    if ((instr & 0x03600000u) == 0x01000000u && rd != 15) {
        lastCycles = 1;
        u32 rn = (instr >> 16) & 0xF;
        u32 baseVal = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
        u32 offset = instr & 0xFFFu;
        u32 targetAddr = (instr & (1u << 23)) ? (baseVal + offset) : (baseVal - offset);
        if (instr & (1u << 20)) {
            u32 val = ((targetAddr & 3u) == 0) ? bus.read32(targetAddr) : readRotatedWord(bus, targetAddr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
            state.r[rd] = val;
        } else {
            bus.write32(targetAddr & ~3u, state.r[rd]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
        }
        return;
    }

    bool isImm = (instr & (1 << 25)) == 0;
    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool isByte = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;

    lastCycles = (isLoad && rd == 15) ? 5 : 1;
    u32 baseVal = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
    u32 offset = 0;

    if (isImm) {
        offset = instr & 0xFFF;
    } else {
        u32 rm = instr & 0xF;
        offset = (rm == 15) ? (state.r[15] + 4) : state.r[rm];
        u32 shiftType = (instr >> 5) & 3;
        u32 shiftAmt = (instr >> 7) & 0x1F;
        bool dummyCarry = (state.cpsr & FLAG_C) != 0;
        offset = shiftOperand(offset, shiftType, shiftAmt, dummyCarry, true);
    }

    u32 targetAddr = pre ? (up ? (baseVal + offset) : (baseVal - offset)) : baseVal;

    if (isLoad) {
        u32 val = isByte ? bus.read8(targetAddr) : readRotatedWord(bus, targetAddr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        if (rd == 15) {
            if (val & 1) {
                state.cpsr |= FLAG_T;
                state.r[15] = val & ~1;
            } else {
                state.cpsr &= ~FLAG_T;
                state.r[15] = val & ~3;
            }
        } else {
            state.r[rd] = val;
        }
    } else {
        u32 val = (rd == 15) ? (state.r[15] + 4) : state.r[rd];
        if (isByte) bus.write8(targetAddr, val & 0xFF);
        else bus.write32(targetAddr & ~3u, val);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
    }

    if (!pre) state.r[rn] = up ? (baseVal + offset) : (baseVal - offset);
    else if (writeback) state.r[rn] = targetAddr;
}

void ARM920T::executeHalfwordTransfer(u32 instr) {
    u32 op = (instr >> 5) & 3;
    if ((instr & 0x01600000u) == 0x01400000u && (op & 1u)) {
        u32 rn = (instr >> 16) & 0xF;
        u32 rd = (instr >> 12) & 0xF;
        u32 baseVal = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
        u32 offset = ((instr >> 4) & 0xF0u) | (instr & 0xFu);
        u32 targetAddr = (instr & (1u << 23)) ? (baseVal + offset) : (baseVal - offset);
        if (instr & (1u << 20)) {
            u16 raw = bus.read16(targetAddr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
            state.r[rd] = (op == 1) ? static_cast<u32>(raw) : static_cast<u32>(static_cast<i32>(static_cast<i16>(raw)));
        } else {
            bus.write16(targetAddr, static_cast<u16>(state.r[rd]));
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
        }
        return;
    }

    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool isImm = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;

    u32 baseVal = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
    u32 offset = 0;

    if (isImm) {
        offset = ((instr >> 8) & 0xF) << 4 | (instr & 0xF);
    } else {
        u32 rm = instr & 0xF;
        offset = (rm == 15) ? (state.r[15] + 4) : state.r[rm];
    }

    u32 targetAddr = pre ? (up ? (baseVal + offset) : (baseVal - offset)) : baseVal;

    if (isLoad) {
        u32 val = 0;
        if (op == 1) val = bus.read16(targetAddr);
        else if (op == 2) val = static_cast<i32>(static_cast<i8>(bus.read8(targetAddr)));
        else if (op == 3) val = static_cast<i32>(static_cast<i16>(bus.read16(targetAddr)));
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        state.r[rd] = val;
    } else {
        if (op == 1) bus.write16(targetAddr, state.r[rd] & 0xFFFF);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
    }

    if (!pre) state.r[rn] = up ? (baseVal + offset) : (baseVal - offset);
    else if (writeback) state.r[rn] = targetAddr;
}

void ARM920T::executeBlockDataTransfer(u32 instr) {
    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool sBit = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 regList = instr & 0xFFFF;

    u32 baseVal = (rn == 15) ? (state.r[15] + 4) : state.r[rn];
    int count = __builtin_popcount(regList);
    lastCycles = std::max(count, 2) + ((isLoad && (regList & 0x8000)) ? 4 : 0);

    u32 startAddr = baseVal;
    if (!up) startAddr = baseVal - (count * 4);
    if (pre == up) startAddr += 4;

    bool userBank = sBit && !(regList & (1 << 15));

    u32 currAddr = startAddr;
    for (u32 list = regList; list != 0; list &= list - 1) {
        const int i = __builtin_ctz(list);
        if (isLoad) {
            u32 val = bus.read32(currAddr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
            if (userBank && i == 13) {
                state.r13_usr = val;
            } else if (userBank && i == 14) {
                state.r14_usr = val;
            } else {
                state.r[i] = val;
            }
            if (i == 15) {
                state.r[15] &= ~3;
                if (sBit) {
                    u32 newCpsr = state.spsr;
                    if ((newCpsr & 0x1F) != (state.cpsr & 0x1F)) {
                        switchMode(newCpsr & 0x1F);
                    }
                    state.cpsr = newCpsr;
                }
            }
        } else {
            u32 val = 0;
            if (userBank && i == 13) val = state.r13_usr;
            else if (userBank && i == 14) val = state.r14_usr;
            else val = (i == 15) ? (state.r[15] + 4) : state.r[i];
            bus.write32(currAddr, val);
            if (bus.getLastFault() != Bus::MmuFault::NONE) {
                handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                bus.clearLastFault();
                return;
            }
        }
        currAddr += 4;
    }

    if (writeback && (!isLoad || !(regList & (1 << rn)))) {
        state.r[rn] = up ? (baseVal + count * 4) : (baseVal - count * 4);
    }
}

void ARM920T::executeMultiply(u32 instr) {
    bool isLong = (instr & (1 << 23)) != 0;
    bool accumulate = (instr & (1 << 21)) != 0;
    bool setCond = (instr & (1 << 20)) != 0;
    u32 rd = (instr >> 16) & 0xF;
    u32 rn = (instr >> 12) & 0xF;
    u32 rs = (instr >> 8) & 0xF;
    u32 rm = instr & 0xF;

    if (cycleTiming) {
    const u32 multiplier = state.r[rs];
    const bool signedTermination = !isLong || (instr & (1u << 22));
    unsigned extra = 3;
    for (unsigned bytes = 1; bytes <= 3; ++bytes) {
        const u32 upper = multiplier >> (bytes * 8);
        const u32 ones = 0xffffffffu >> (bytes * 8);
        if (upper == 0 || (signedTermination && upper == ones)) { extra = bytes - 1; break; }
    }
    lastCycles = (isLong ? 3 : 2) + extra;
    }
    if (!isLong) {
        u32 res = state.r[rm] * state.r[rs];
        if (accumulate) res += state.r[rn];
        state.r[rd] = res;
        if (setCond) setNZFlags(res);
    } else {
        bool isSigned = (instr & (1 << 22)) != 0;
        u64 res = 0;
        if (isSigned) {
            i64 a = static_cast<i32>(state.r[rm]);
            i64 b = static_cast<i32>(state.r[rs]);
            i64 rVal = a * b;
            if (accumulate) rVal += (static_cast<u64>(state.r[rd]) << 32) | state.r[rn];
            res = static_cast<u64>(rVal);
        } else {
            res = static_cast<u64>(state.r[rm]) * static_cast<u64>(state.r[rs]);
            if (accumulate) res += (static_cast<u64>(state.r[rd]) << 32) | state.r[rn];
        }
        state.r[rn] = res & 0xFFFFFFFF;
        state.r[rd] = (res >> 32) & 0xFFFFFFFF;
        if (setCond) {
            state.cpsr &= ~(FLAG_N | FLAG_Z);
            if (res & 0x8000000000000000ULL) state.cpsr |= FLAG_N;
            if (res == 0) state.cpsr |= FLAG_Z;
        }
    }
}

void ARM920T::executeSwap(u32 instr) {
    lastCycles = 2;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;
    u32 rm = instr & 0xF;
    u32 addr = state.r[rn];
    bool isByte = (instr & (1 << 22)) != 0;

    if (isByte) {
        u8 temp = bus.read8(addr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        bus.write8(addr, state.r[rm] & 0xFF);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        state.r[rd] = temp;
    } else {
        u32 temp = readRotatedWord(bus, addr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        bus.write32(addr & ~3u, state.r[rm]);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        state.r[rd] = temp;
    }
}

void ARM920T::executeSWI(u32 instr) {
    lastEvent = StepEvent::SupervisorCall;
    lastCycles = 3;
    u32 swiNum = instr & 0x00FFFFFF;
    u32 retAddr = state.r[15];
    u32 oldCpsr = state.cpsr;
    if (syscallObserver && (oldCpsr & 31u) == 0x10 && bus.isMmuEnabled()) {
        const u32 number = swiNum ? (swiNum & 0xfffffu) : state.r[7];
        syscallObserver(number, retAddr - ((oldCpsr & FLAG_T) ? 2u : 4u),
                        {state.r[0], state.r[1], state.r[2], state.r[3], state.r[4], state.r[5]});
    }
    switchMode(0x13); // Supervisor mode
    state.spsr = oldCpsr;
    state.cpsr |= FLAG_I;              // Disable IRQ
    state.cpsr &= ~FLAG_T;             // ARM state
    state.r[14] = retAddr;             // Save return address
    state.r[15] = (state.cp15_control & (1 << 13)) ? 0xFFFF0008 : 0x00000008;

    if (debugLogging && bus.isMmuEnabled()) {
        u32 nr = swiNum & 0x000FFFFF;
        if (nr == 0x0b) { // execve
            std::string fn;
            for (int i = 0; i < 64; ++i) {
                u8 ch = 0;
                if (!bus.peek8(state.r[0] + i, ch) || ch == 0) break;
                fn += static_cast<char>(ch);
            }
            std::cout << "\n>>> [USERSPACE EXECVE] \"" << fn << "\" <<<\n" << std::endl;
        } else if (nr == 0x05) { // open
            std::string fn;
            for (int i = 0; i < 128; ++i) {
                u8 ch = 0;
                if (!bus.peek8(state.r[0] + i, ch) || ch == 0) break;
                fn += static_cast<char>(ch);
            }
            std::cout << "[USERSPACE OPEN r0=0x" << std::hex << state.r[0] << "] \"" << fn << "\" flags=0x" << state.r[1] << std::dec << std::endl;
        } else if (nr == 0x04) { // write
            std::string out;
            u32 len = std::min(state.r[2], 128u);
            for (u32 i = 0; i < len; ++i) {
                u8 ch = 0;
                if (!bus.peek8(state.r[1] + i, ch)) break;
                out += static_cast<char>(ch);
            }
            std::cout << "[USERSPACE WRITE fd=" << state.r[0] << "] \"" << out << "\"" << std::endl;
        } else if (nr == 0x36) { // ioctl
            std::cout << "[USERSPACE IOCTL] fd=" << state.r[0] << " cmd=0x" << std::hex << state.r[1] << " arg=0x" << state.r[2] << std::dec << std::endl;
        } else if (nr == 0xc0) { // mmap2
            std::cout << "[USERSPACE MMAP2] addr=0x" << std::hex << state.r[0] << " len=0x" << state.r[1]
                      << " prot=0x" << state.r[2] << " flags=0x" << state.r[3] << " fd=" << std::dec << (int)state.r[4]
                      << " pgoff=0x" << std::hex << state.r[5] << std::dec << std::endl;
        } else {
            std::cout << "[SWI] Syscall 0x" << std::hex << swiNum << " called from 0x" << (retAddr - 4)
                      << " -> vector 0x" << state.r[15] << " r0=0x" << state.r[0] << " r1=0x" << state.r[1]
                      << " r2=0x" << state.r[2] << std::dec << std::endl;
        }
    }
}

void ARM920T::stepThumb(u16 instr) {
    u32 instrPC = state.r[15];
    state.r[15] += 2;

    // Format 2: Add/subtract (register / 3-bit immediate)
    if ((instr & 0xF800) == 0x1800) {
        bool isImm = (instr & (1 << 10)) != 0;
        bool isSub = (instr & (1 << 9)) != 0;
        u32 rn = (instr >> 6) & 7;
        u32 rs = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 op1 = state.r[rs];
        u32 op2 = isImm ? rn : state.r[rn];
        u32 res = isSub ? (op1 - op2) : (op1 + op2);
        state.r[rd] = res;
        if (isSub) setSubFlags(op1, op2, res);
        else setAddFlags(op1, op2, res);
        return;
    }

    // Format 1: Move shifted register
    if ((instr & 0xE000) == 0x0000) {
        u32 subOp = (instr >> 11) & 3;
        u32 offset = (instr >> 6) & 0x1F;
        u32 rs = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 val = state.r[rs];
        u32 res = 0;
        bool carry = (state.cpsr & FLAG_C) != 0;

        if (subOp == 0) { // LSL
            if (offset == 0) {
                res = val; // MOV Rd, Rs (carry unaffected)
            } else {
                carry = (val >> (32 - offset)) & 1;
                res = val << offset;
                if (carry) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
            }
        } else if (subOp == 1) { // LSR
            if (offset == 0) { // LSR #32
                carry = (val >> 31) & 1;
                res = 0;
            } else {
                carry = (val >> (offset - 1)) & 1;
                res = val >> offset;
            }
            if (carry) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
        } else if (subOp == 2) { // ASR
            if (offset == 0) { // ASR #32
                carry = (val >> 31) & 1;
                res = (val & 0x80000000) ? 0xFFFFFFFF : 0;
            } else {
                carry = (static_cast<i32>(val) >> (offset - 1)) & 1;
                res = static_cast<u32>(static_cast<i32>(val) >> offset);
            }
            if (carry) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
        }
        state.r[rd] = res;
        setNZFlags(res);
        return;
    }

    // Format 3: Move/compare/add/subtract immediate
    if ((instr & 0xE000) == 0x2000) {
        u32 subOp = (instr >> 11) & 3;
        u32 rd = (instr >> 8) & 7;
        u32 imm = instr & 0xFF;
        if (subOp == 0) { // MOV
            state.r[rd] = imm;
            setNZFlags(imm);
        } else if (subOp == 1) { // CMP
            setSubFlags(state.r[rd], imm, state.r[rd] - imm);
        } else if (subOp == 2) { // ADD
            u32 op1 = state.r[rd];
            u32 res = op1 + imm;
            state.r[rd] = res;
            setAddFlags(op1, imm, res);
        } else if (subOp == 3) { // SUB
            u32 op1 = state.r[rd];
            u32 res = op1 - imm;
            state.r[rd] = res;
            setSubFlags(op1, imm, res);
        }
        return;
    }

    // Format 4: ALU operations
    if ((instr & 0xFC00) == 0x4000) {
        u32 aluOp = (instr >> 6) & 0xF;
        u32 rs = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 op1 = state.r[rd];
        u32 op2 = state.r[rs];
        switch (aluOp) {
            case 0x0: { // AND
                state.r[rd] = op1 & op2;
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x1: { // EOR
                state.r[rd] = op1 ^ op2;
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x2: { // LSL
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    state.r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (op1 >> (32 - amt)) & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = op1 << amt;
                } else if (amt == 32) {
                    bool c = op1 & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = 0;
                } else {
                    state.cpsr &= ~FLAG_C;
                    state.r[rd] = 0;
                }
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x3: { // LSR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    state.r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (op1 >> (amt - 1)) & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = op1 >> amt;
                } else if (amt == 32) {
                    bool c = (op1 >> 31) & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = 0;
                } else {
                    state.cpsr &= ~FLAG_C;
                    state.r[rd] = 0;
                }
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x4: { // ASR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    state.r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (static_cast<i32>(op1) >> (amt - 1)) & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = static_cast<u32>(static_cast<i32>(op1) >> amt);
                } else {
                    bool c = (op1 >> 31) & 1;
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = (op1 & 0x80000000) ? 0xFFFFFFFF : 0;
                }
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x5: { // ADC
                u32 c = (state.cpsr & FLAG_C) ? 1 : 0;
                u32 res = op1 + op2 + c;
                state.r[rd] = res;
                setNZFlags(res);
                if (static_cast<u64>(op1) + op2 + c > 0xFFFFFFFFULL) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                if (((op1 ^ res) & (op2 ^ res) & 0x80000000) != 0) state.cpsr |= FLAG_V; else state.cpsr &= ~FLAG_V;
                break;
            }
            case 0x6: { // SBC
                u32 notC = (state.cpsr & FLAG_C) ? 0 : 1;
                u32 res = op1 - op2 - notC;
                state.r[rd] = res;
                setNZFlags(res);
                if (static_cast<u64>(op1) >= static_cast<u64>(op2) + notC) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                if (((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0) state.cpsr |= FLAG_V; else state.cpsr &= ~FLAG_V;
                break;
            }
            case 0x7: { // ROR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    state.r[rd] = op1;
                } else {
                    u32 amt5 = amt & 0x1F;
                    bool c = (amt5 == 0) ? ((op1 >> 31) & 1) : ((op1 >> (amt5 - 1)) & 1);
                    if (c) state.cpsr |= FLAG_C; else state.cpsr &= ~FLAG_C;
                    state.r[rd] = (amt5 == 0) ? op1 : ((op1 >> amt5) | (op1 << (32 - amt5)));
                }
                setNZFlags(state.r[rd]);
                break;
            }
            case 0x8: { // TST
                setNZFlags(op1 & op2);
                break;
            }
            case 0x9: { // NEG
                u32 res = 0 - op2;
                state.r[rd] = res;
                setSubFlags(0, op2, res);
                break;
            }
            case 0xA: { // CMP
                setSubFlags(op1, op2, op1 - op2);
                break;
            }
            case 0xB: { // CMN
                setAddFlags(op1, op2, op1 + op2);
                break;
            }
            case 0xC: { // ORR
                state.r[rd] = op1 | op2;
                setNZFlags(state.r[rd]);
                break;
            }
            case 0xD: { // MUL
                const u32 v = op2;
                lastCycles = (v >> 8 == 0 || v >> 8 == 0xffffff) ? 2 :
                             (v >> 16 == 0 || v >> 16 == 0xffff) ? 3 :
                             (v >> 24 == 0 || v >> 24 == 0xff) ? 4 : 5;
                state.r[rd] = op1 * op2;
                setNZFlags(state.r[rd]);
                break;
            }
            case 0xE: { // BIC
                state.r[rd] = op1 & ~op2;
                setNZFlags(state.r[rd]);
                break;
            }
            case 0xF: { // MVN
                state.r[rd] = ~op2;
                setNZFlags(state.r[rd]);
                break;
            }
        }
        return;
    }

    // Format 5: Hi register operations / branch exchange
    if ((instr & 0xFC00) == 0x4400) {
        u32 op5 = (instr >> 8) & 3;
        u32 d = (instr & 7) | ((instr & 0x80) >> 4);
        u32 s = ((instr >> 3) & 7) | ((instr & 0x40) >> 3);
        if (op5 == 3 || (d == 15 && op5 != 1)) lastCycles = 3;
        u32 valS = (s == 15) ? (instrPC + 4) : state.r[s];
        u32 valD = (d == 15) ? (instrPC + 4) : state.r[d];
        if (op5 == 0) { // ADD
            u32 res = valD + valS;
            if (d == 15) state.r[15] = res & ~1;
            else state.r[d] = res;
        } else if (op5 == 1) { // CMP
            setSubFlags(valD, valS, valD - valS);
        } else if (op5 == 2) { // MOV
            if (d == 15) state.r[15] = valS & ~1;
            else state.r[d] = valS;
        } else if (op5 == 3) { // BX / BLX
            if (instr & 0x80) { // BLX
                state.r[14] = (instrPC + 2) | 1;
            }
            if (valS & 1) {
                state.cpsr |= FLAG_T;
                state.r[15] = valS & ~1;
            } else {
                state.cpsr &= ~FLAG_T;
                state.r[15] = valS & ~3;
            }
        }
        return;
    }

    // Format 6: PC-relative load
    if ((instr & 0xF800) == 0x4800) {
        u32 rd = (instr >> 8) & 7;
        u32 imm = (instr & 0xFF) * 4;
        u32 addr = ((instrPC + 4) & ~3) + imm;
        u32 val = bus.read32(addr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        state.r[rd] = val;
        return;
    }

    // Format 7 & Format 8: Load/store register offset / sign-extended byte/halfword
    if ((instr & 0xF000) == 0x5000) {
        bool signedOrHalfword = (instr & (1 << 9)) != 0;
        u32 ro = (instr >> 6) & 7;
        u32 rb = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 addr = state.r[rb] + state.r[ro];
        if (!signedOrHalfword) { // Format 7: Load/store with register offset
            bool load = (instr & (1 << 11)) != 0;
            bool isByte = (instr & (1 << 10)) != 0;
            if (load) {
                if (isByte) {
                    u8 val = bus.read8(addr);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                    state.r[rd] = val;
                } else {
                    u32 val = readRotatedWord(bus, addr);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                    state.r[rd] = val;
                }
            } else {
                if (isByte) {
                    bus.write8(addr, state.r[rd] & 0xFF);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                } else {
                    bus.write32(addr & ~3u, state.r[rd]);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                }
            }
        } else { // Format 8: Load/store sign-extended byte/halfword
            u32 op8 = (instr >> 10) & 3;
            if (op8 == 0) { // STRH
                bus.write16(addr, state.r[rd] & 0xFFFF);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            } else if (op8 == 1) { // LDSB
                u8 val = bus.read8(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                state.r[rd] = static_cast<u32>(static_cast<i8>(val));
            } else if (op8 == 2) { // LDRH
                u16 val = bus.read16(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                state.r[rd] = val;
            } else if (op8 == 3) { // LDSH
                u16 val = bus.read16(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                state.r[rd] = static_cast<u32>(static_cast<i16>(val));
            }
        }
        return;
    }

    // Format 9: Load/store with immediate offset
    if ((instr & 0xE000) == 0x6000) {
        bool isByte = (instr & (1 << 12)) != 0;
        bool load = (instr & (1 << 11)) != 0;
        u32 offset5 = (instr >> 6) & 0x1F;
        u32 rb = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 addr = state.r[rb] + (isByte ? offset5 : (offset5 * 4));
        if (load) {
            if (isByte) {
                u8 val = bus.read8(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                state.r[rd] = val;
            } else {
                u32 val = readRotatedWord(bus, addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                state.r[rd] = val;
            }
        } else {
            if (isByte) {
                bus.write8(addr, state.r[rd] & 0xFF);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            } else {
                bus.write32(addr & ~3u, state.r[rd]);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            }
        }
        return;
    }

    // Format 10: Load/store halfword
    if ((instr & 0xF000) == 0x8000) {
        bool load = (instr & (1 << 11)) != 0;
        u32 offset5 = (instr >> 6) & 0x1F;
        u32 rb = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 addr = state.r[rb] + (offset5 * 2);
        if (load) {
            u16 val = bus.read16(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            state.r[rd] = val;
        } else {
            bus.write16(addr, state.r[rd] & 0xFFFF);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }

    // Format 11: SP-relative load/store
    if ((instr & 0xF000) == 0x9000) {
        bool load = (instr & (1 << 11)) != 0;
        u32 rd = (instr >> 8) & 7;
        u32 imm = (instr & 0xFF) * 4;
        u32 addr = state.r[13] + imm;
        if (load) {
            u32 val = bus.read32(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            state.r[rd] = val;
        } else {
            bus.write32(addr, state.r[rd]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }

    // Format 12: Load address
    if ((instr & 0xF000) == 0xA000) {
        bool sp = (instr & (1 << 11)) != 0;
        u32 rd = (instr >> 8) & 7;
        u32 imm = (instr & 0xFF) * 4;
        if (sp) state.r[rd] = state.r[13] + imm;
        else state.r[rd] = ((instrPC + 4) & ~3) + imm;
        return;
    }

    // Format 13: Add offset to Stack Pointer
    if ((instr & 0xFF00) == 0xB000) {
        bool sub = (instr & (1 << 7)) != 0;
        u32 imm = (instr & 0x7F) * 4;
        if (sub) state.r[13] -= imm;
        else state.r[13] += imm;
        return;
    }

    // Format 14: Push/Pop registers
    if ((instr & 0xFE00) == 0xB400) { // PUSH
        const u32 count = __builtin_popcount(instr & 0x1FFu);
        lastCycles = std::max(count, 2u);
        bool rBit = (instr & (1 << 8)) != 0; // LR
        u32 addr = state.r[13] - count * 4;
        state.r[13] = addr;
        for (u32 list = instr & 0xFFu; list != 0; list &= list - 1) {
            const int i = __builtin_ctz(list);
            bus.write32(addr, state.r[i]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            addr += 4;
        }
        if (rBit) {
            bus.write32(addr, state.r[14]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }
    if ((instr & 0xFE00) == 0xBC00) { // POP
        const unsigned count = __builtin_popcount(instr & 0x1FFu);
        lastCycles = std::max(count, 2u) + ((instr & 0x100) ? 4 : 0);
        bool rBit = (instr & (1 << 8)) != 0; // PC
        u32 addr = state.r[13];
        for (u32 list = instr & 0xFFu; list != 0; list &= list - 1) {
            const int i = __builtin_ctz(list);
            state.r[i] = bus.read32(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            addr += 4;
        }
        if (rBit) {
            u32 target = bus.read32(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            addr += 4;
            if (target & 1) {
                state.cpsr |= FLAG_T;
                state.r[15] = target & ~1;
            } else {
                state.cpsr &= ~FLAG_T;
                state.r[15] = target & ~3;
            }
        }
        state.r[13] = addr;
        return;
    }

    // Format 15: Multiple load/store
    if ((instr & 0xF000) == 0xC000) {
        const unsigned count = __builtin_popcount(instr & 0xFFu);
        lastCycles = std::max(count, 2u);
        bool load = (instr & (1 << 11)) != 0;
        u32 rb = (instr >> 8) & 7;
        u32 addr = state.r[rb];
        for (u32 list = instr & 0xFFu; list != 0; list &= list - 1) {
            const int i = __builtin_ctz(list);
            if (load) {
                state.r[i] = bus.read32(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            } else {
                bus.write32(addr, state.r[i]);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            }
            addr += 4;
        }
        if (!load || !(instr & (1 << rb))) {
            state.r[rb] = addr;
        }
        return;
    }

    // Format 17: Software Interrupt
    if ((instr & 0xFF00) == 0xDF00) {
        executeSWI(instr & 0xFF);
        return;
    }

    // Format 16: Conditional branch
    if ((instr & 0xF000) == 0xD000) {
        u32 cond = (instr >> 8) & 0xF;
        if (cond <= 0xD) {
            if (evaluateCondition(cond)) {
                lastCycles = 3;
                i32 offset = static_cast<i32>(static_cast<i8>(instr & 0xFF)) * 2;
                state.r[15] = (instrPC + 4) + offset;
            }
        }
        return;
    }

    // Format 18: Unconditional branch
    if ((instr & 0xF800) == 0xE000) {
        lastCycles = 3;
        i32 off = instr & 0x7FF;
        if (off & 0x400) off |= ~0x7FF;
        state.r[15] = (instrPC + 4) + (off * 2);
        return;
    }

    // Format 19: Long branch with link / BLX
    if ((instr & 0xE000) == 0xE000) {
        u32 op19 = (instr >> 11) & 0x1F;
        if (op19 == 0x1E) { // BL prefix
            i32 off = instr & 0x7FF;
            if (off & 0x400) off |= ~0x7FF;
            state.r[14] = (instrPC + 4) + (off << 12);
            return;
        } else if (op19 == 0x1F) { // BL suffix
            lastCycles = 3;
            u32 target = state.r[14] + ((instr & 0x7FF) << 1);
            state.r[14] = state.r[15] | 1;
            state.r[15] = target;
            return;
        } else if (op19 == 0x1D) { // BLX suffix
            lastCycles = 3;
            u32 target = (state.r[14] + ((instr & 0x7FF) << 1)) & ~3;
            state.r[14] = state.r[15] | 1;
            state.cpsr &= ~FLAG_T;
            state.r[15] = target;
            return;
        }
    }
}

std::string ARM920T::disassembleCurrentARM() const {
    std::stringstream ss;
    u32 instr = 0;
    if (bus.peek32(state.r[15], instr)) {
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << state.r[15] << ": "
           << std::setw(8) << instr;
    } else {
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << state.r[15] << ": [unmapped]";
    }
    return ss.str();
}

} // namespace oceanblast
