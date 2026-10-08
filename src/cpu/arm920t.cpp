#include "arm920t.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>

namespace oceanblast {

// ARMv4T word loads rotate an aligned word; they do not join bytes from two words.
static u32 readRotatedWord(Bus& bus, u32 address) {
    const u32 word = bus.read32(address & ~3u);
    const u32 shift = (address & 3u) * 8;
    return shift ? (word >> shift) | (word << (32 - shift)) : word;
}

ARM920T::ARM920T(Bus& bus) : bus(bus), cpsr(0x00000013), spsr(0), halted(false) {
    reset();
}

ARM920T::~ARM920T() {}

void ARM920T::reset(u32 startAddress) {
    std::memset(r, 0, sizeof(r));
    r[15] = startAddress; // Reset vector (typically 0x00000000 in Steppingstone SRAM)
    r[13] = 0x00000F00;   // Boot stack pointer in Steppingstone SRAM
    cpsr  = 0x000000D3;   // Supervisor (SVC32) mode, ARM state, IRQ/FIQ disabled
    spsr  = 0;
    halted = false;

    r13_usr = r14_usr = 0;
    r13_svc = r[13]; r14_svc = 0; spsr_svc = 0;
    r13_irq = r14_irq = spsr_irq = 0;
    r13_abt = r14_abt = spsr_abt = 0;
    r13_und = r14_und = spsr_und = 0;

    cp15_control = 0x00000070;
    cp15_ttb     = 0;
    cp15_dacr    = 0;
    bus.setMmuEnabled(false);
    bus.setTtb(0);
    bus.setDacr(0);
    bus.setUserMode(false);
}

void ARM920T::switchMode(u32 newMode) {
    u32 oldMode = cpsr & 0x1F;
    if (newMode == oldMode) return;

    // 1. Save state of current mode
    switch (oldMode) {
        case 0x10: // USR
        case 0x1F: // SYS
            r13_usr = r[13];
            r14_usr = r[14];
            break;
        case 0x13: // SVC
            r13_svc = r[13];
            r14_svc = r[14];
            spsr_svc = spsr;
            break;
        case 0x12: // IRQ
            r13_irq = r[13];
            r14_irq = r[14];
            spsr_irq = spsr;
            break;
        case 0x17: // ABT
            r13_abt = r[13];
            r14_abt = r[14];
            spsr_abt = spsr;
            break;
        case 0x1B: // UND
            r13_und = r[13];
            r14_und = r[14];
            spsr_und = spsr;
            break;
        default:
            break;
    }

    // 2. Restore state of new mode
    switch (newMode) {
        case 0x10: // USR
        case 0x1F: // SYS
            r[13] = r13_usr;
            r[14] = r14_usr;
            break;
        case 0x13: // SVC
            r[13] = r13_svc;
            r[14] = r14_svc;
            spsr = spsr_svc;
            break;
        case 0x12: // IRQ
            r[13] = r13_irq;
            r[14] = r14_irq;
            spsr = spsr_irq;
            break;
        case 0x17: // ABT
            r[13] = r13_abt;
            r[14] = r14_abt;
            spsr = spsr_abt;
            break;
        case 0x1B: // UND
            r[13] = r13_und;
            r[14] = r14_und;
            spsr = spsr_und;
            break;
        default:
            break;
    }

    cpsr = (cpsr & ~0x1F) | (newMode & 0x1F);
    bus.setUserMode((cpsr & 0x1F) == 0x10);
}

void ARM920T::handleIrq() {
    u32 oldCpsr = cpsr;
    switchMode(0x12); // IRQ mode
    spsr = oldCpsr;
    r[14] = r[15] + 4; // Return address for `sub lr, lr, #4`
    cpsr |= FLAG_I;   // Disable further IRQs
    cpsr &= ~FLAG_T;  // ARM state
    r[15] = (cp15_control & (1 << 13)) ? 0xFFFF0018 : 0x00000018;
}

void ARM920T::handlePrefetchAbort(u32 faultPC) {
    if (faultLogging) logFaultContext("prefetch", faultPC, faultPC);
    u32 retAddr = faultPC + 4;
    u32 oldCpsr = cpsr;
    switchMode(0x17); // Abort mode
    spsr = oldCpsr;
    cpsr |= FLAG_I;   // Disable IRQ
    cpsr &= ~FLAG_T;  // ARM state
    r[14] = retAddr;  // r14_abt = faultPC + 4
    r[15] = (cp15_control & (1 << 13)) ? 0xFFFF000C : 0x0000000C;

    static int pabtCount = 0;
    if (pabtCount++ < 30 && debugLogging) {
        std::cout << "[PREFETCH ABORT #" << pabtCount << "] faultPC=0x" << std::hex << faultPC
                  << " -> vector 0x" << r[15] << " retAddr=0x" << retAddr << std::dec << std::endl;
    }
}

void ARM920T::handleDataAbort(u32 faultAddr, Bus::MmuFault faultType) {
    if (faultLogging) logFaultContext("data", r[15] - (isThumb() ? 2 : 4), faultAddr);
    cp15_far = faultAddr;
    cp15_fsr = static_cast<u32>(faultType);
    u32 retAddr = (cpsr & FLAG_T) ? (r[15] + 6) : (r[15] + 4); // instruction_pc + 8
    u32 oldCpsr = cpsr;
    switchMode(0x17); // Abort mode
    spsr = oldCpsr;
    cpsr |= FLAG_I;   // Disable IRQ
    cpsr &= ~FLAG_T;  // ARM state
    r[14] = retAddr;  // r14_abt = instruction_pc + 8
    r[15] = (cp15_control & (1 << 13)) ? 0xFFFF0010 : 0x00000010;

    static int dabtCount = 0;
    if (dabtCount++ < 50 && debugLogging) {
        std::cout << "[DATA ABORT #" << dabtCount << "] faultAddr=0x" << std::hex << faultAddr
                  << " fsr=0x" << cp15_fsr << " -> vector 0x" << r[15]
                  << " retAddr=0x" << retAddr << std::dec << std::endl;
    }
}

void ARM920T::handleUndefinedInstruction(u32 instr) {
    if (faultLogging) logFaultContext("undefined", r[15] - (isThumb() ? 2 : 4), instr);
    u32 retAddr = r[15]; // address after the undefined instruction
    u32 oldCpsr = cpsr;
    switchMode(0x1B); // UND mode
    spsr = oldCpsr;
    cpsr |= FLAG_I;   // Disable IRQ
    cpsr &= ~FLAG_T;  // ARM state
    r[14] = retAddr;  // r14_und = instruction_pc + 4
    r[15] = (cp15_control & (1 << 13)) ? 0xFFFF0004 : 0x00000004;

    static int undCount = 0;
    if (undCount++ < 15 && debugLogging) {
        std::cout << "[UNDEF INSTR #" << undCount << "] instr=0x" << std::hex << instr
                  << " at 0x" << (retAddr - 4) << " -> vector 0x" << r[15] << std::dec << std::endl;
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
    std::cout << "[CPU] PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << r[15]
              << " SP: 0x" << std::setw(8) << r[13]
              << " LR: 0x" << std::setw(8) << r[14]
              << " CPSR: 0x" << std::setw(8) << cpsr
              << (isThumb() ? " (Thumb)" : " (ARM)") << std::dec << std::endl;
    std::cout << "  r12: 0x" << std::hex << std::setw(8) << r[12] << std::dec << '\n';
    for (int i = 0; i < 12; i += 4) {
        std::cout << "  r" << i << ": 0x" << std::hex << std::setw(8) << r[i]
                  << "  r" << std::dec << (i+1) << ": 0x" << std::hex << std::setw(8) << r[i+1]
                  << "  r" << std::dec << (i+2) << ": 0x" << std::hex << std::setw(8) << r[i+2]
                  << "  r" << std::dec << (i+3) << ": 0x" << std::hex << std::setw(8) << r[i+3] << std::dec << std::endl;
    }
}

void ARM920T::step(size_t peripheralTicks) {
    if (halted) return;

    if (!(cpsr & FLAG_I) && bus.hasPendingIrq()) {
        handleIrq();
        return;
    }

    u32 currentPC = r[15];
    Bus::MmuFault fetchFault = Bus::MmuFault::NONE;
    u32 pa = bus.translate(currentPC, &fetchFault);
    if (fetchFault != Bus::MmuFault::NONE) {
        handlePrefetchAbort(currentPC);
        bus.tick(peripheralTicks);
        return;
    }

    if (isThumb()) {
        stepThumb(pa);
    } else {
        stepARM(pa);
    }

    bus.tick(peripheralTicks);
}

bool ARM920T::evaluateCondition(u32 cond) const {
    bool N = (cpsr & FLAG_N) != 0;
    bool Z = (cpsr & FLAG_Z) != 0;
    bool C = (cpsr & FLAG_C) != 0;
    bool V = (cpsr & FLAG_V) != 0;

    switch (cond) {
        case 0x0: return Z;                      // EQ
        case 0x1: return !Z;                     // NE
        case 0x2: return C;                      // CS / HS
        case 0x3: return !C;                     // CC / LO
        case 0x4: return N;                      // MI
        case 0x5: return !N;                     // PL
        case 0x6: return V;                      // VS
        case 0x7: return !V;                     // VC
        case 0x8: return C && !Z;                // HI
        case 0x9: return !C || Z;                // LS
        case 0xA: return N == V;                 // GE
        case 0xB: return N != V;                 // LT
        case 0xC: return !Z && (N == V);         // GT
        case 0xD: return Z || (N != V);          // LE
        case 0xE: return true;                   // AL (Always)
        case 0xF: return true;                   // NV / Reserved
        default:  return false;
    }
}

void ARM920T::setNZFlags(u32 result) {
    cpsr &= ~(FLAG_N | FLAG_Z);
    if (result & 0x80000000) cpsr |= FLAG_N;
    if (result == 0)          cpsr |= FLAG_Z;
}

void ARM920T::setAddFlags(u32 a, u32 b, u32 res) {
    setNZFlags(res);
    if (static_cast<u64>(a) + static_cast<u64>(b) > 0xFFFFFFFFULL) cpsr |= FLAG_C;
    else cpsr &= ~FLAG_C;
    if (((a ^ res) & (b ^ res) & 0x80000000) != 0) cpsr |= FLAG_V;
    else cpsr &= ~FLAG_V;
}

void ARM920T::setSubFlags(u32 a, u32 b, u32 res) {
    setNZFlags(res);
    if (a >= b) cpsr |= FLAG_C;
    else cpsr &= ~FLAG_C;
    if (((a ^ b) & (a ^ res) & 0x80000000) != 0) cpsr |= FLAG_V;
    else cpsr &= ~FLAG_V;
}

u32 ARM920T::shiftOperand(u32 val, u32 type, u32 amount, bool& carryOut, bool immediate) {
    carryOut = (cpsr & FLAG_C) != 0;
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

void ARM920T::stepARM(u32 physAddr) {
    u32 pc = r[15];
    if (debugLogging) {
    if (pc == 0xc001a538) {
        static int irqDbgCount = 0;
        if (irqDbgCount++ < 10) {
            std::cout << "[IRQ_DISPATCH] r0=" << std::hex << r[0] << " r1=" << r[1]
                      << " [r1+0]=" << bus.read32(r[1])
                      << " [r1+4]=" << bus.read32(r[1]+4)
                      << " [r1+8]=" << bus.read32(r[1]+8) << std::dec << std::endl;
        }
    }
    if (pc == 0xc001a540) {
        static int irqCallCount = 0;
        if (irqCallCount++ < 10) {
            std::cout << "[IRQ_CALL] r3=" << std::hex << r[3] << " [r3]=" << bus.read32(r[3]) << std::dec << std::endl;
        }
    }
    if (pc == 0xc00198c8) {
        std::cout << "[RET_TO_USER] sp=" << std::hex << r[13]
                  << " [sp+0x3c]=" << bus.read32(r[13] + 0x3c)
                  << " [sp+0x40]=" << bus.read32(r[13] + 0x40)
                  << " cpsr=" << cpsr << std::dec << std::endl;
    }
    if (pc == 0xc00703ac) {
        std::cout << "[EXEC_MMAP] r9=0x" << std::hex << r[9] << " [r9+0x100]=0x" << bus.read32(r[9] + 0x100) << std::dec << std::endl;
    }
    if (pc == 0xc006ffc0) {
        std::cout << "[DO_EXECVE mm_alloc call]" << std::endl;
    }
    if (pc == 0xc006ffc4) {
        std::cout << "[DO_EXECVE mm_alloc ret] mm=0x" << std::hex << r[0]
                  << " pgd=0x" << bus.read32(r[0] + 0x1c) << std::dec << std::endl;
    }
    if (pc == 0xc0070420) {
        std::cout << "[PRE_SWITCH_MM] r4=0x" << std::hex << r[4]
                  << " [r4+0x1c]=0x" << bus.read32(r[4] + 0x1c)
                  << " r5=0x" << r[5] << " r6=0x" << r[6]
                  << " LR=0x" << r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0021be8) {
        std::cout << "[SWITCH_MM] new TTB=0x" << std::hex << r[0] << " lr=0x" << r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0125180) {
        std::cout << "[ADC PROBE CALLED] r0=0x" << std::hex << r[0] << " lr=0x" << r[14] << std::dec << std::endl;
    }
    if (pc == 0xc0125020 || pc == 0xc0124f20) {
        std::cout << "[ADC LOCK] PC=0x" << std::hex << pc << " LR=0x" << r[14]
                  << " r0=0x" << r[0] << " sem_count=" << (i32)bus.read32(r[0])
                  << " CPSR=0x" << cpsr << std::dec << std::endl;
    }
    if (pc == 0xc0147660) {
        static int schedEntryCount = 0;
        std::cout << "[SCHEDULE_ENTRY #" << schedEntryCount << "] LR=0x" << std::hex << r[14]
                  << " SP=0x" << r[13] << " CPSR=0x" << cpsr << std::dec << std::endl;
        if (schedEntryCount == 52) {
            std::cout << "--- [STACK DUMP at SCHEDULE_ENTRY #52] ---" << std::hex << "\n";
            for (u32 s = r[13]; s < r[13] + 160; s += 4) {
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
            comm10[i] = bus.read8(r[10] + 0x1a4 + i);
            comm8[i] = bus.read8(r[8] + 0x1a4 + i);
        }
        std::cout << "[SCHEDULE_SWITCH] next='" << comm10 << "' (0x" << std::hex << r[10]
                  << ") prev='" << comm8 << "' (0x" << r[8] << ")" << std::dec << std::endl;
    }
    if (pc == 0xc0019844) {
        std::cout << "[SWITCH_TO] r1=" << std::hex << r[1] << " r2=" << r[2] << "\n";
        for (int i = 0; i <= 40; i += 4) {
            std::cout << "  [r2 + " << i << "] = 0x" << bus.read32(r[2] + i) << "\n";
        }
        std::cout << std::dec;
    }
    }
    u32 instr = (physAddr != 0xFFFFFFFF) ? bus.read32Phys(physAddr) : bus.read32(pc);
    r[15] += 4; // Advance PC to instruction address + 4

    u32 cond = instr >> 28;
    if (!evaluateCondition(cond)) return;

    // 1. BX / BLX
    if ((instr & 0x0FFFFFF0) == 0x012FFF10 || (instr & 0x0FFFFFF0) == 0x012FFF30) {
        executeBX(instr);
    }
    // 2. MRS
    else if ((instr & 0x0FBF0FFF) == 0x010F0000) {
        executeMRS(instr);
    }
    // 3. MSR (Register)
    else if ((instr & 0x0FB0F000) == 0x0120F000) {
        executeMSR(instr);
    }
    // 4. MSR (Immediate)
    else if ((instr & 0x0FB0F000) == 0x0320F000) {
        executeMSR(instr);
    }
    // 5. Swap (SWP / SWPB)
    else if ((instr & 0x0FB00FF0) == 0x01000090) {
        executeSwap(instr);
    }
    // 6. Multiply
    else if ((instr & 0x0F000090) == 0x00000090 && ((instr & 0x00000060) == 0)) {
        executeMultiply(instr);
    }
    // 7. Halfword Data Transfer
    else if ((instr & 0x0E000090) == 0x00000090 && ((instr & 0x00000060) != 0)) {
        executeHalfwordTransfer(instr);
    }
    // 7. SWI
    else if ((instr & 0x0F000000) == 0x0F000000) {
        executeSWI(instr);
    }
    // 8. Coprocessor 15 (System Control)
    else if ((instr & 0x0F000010) == 0x0E000010 && (((instr >> 8) & 0xF) == 15)) {
        executeCP15(instr);
    }
    // 9. Branch B / BL
    else if ((instr & 0x0E000000) == 0x0A000000) {
        executeBranch(instr);
    }
    // 10. Block Data Transfer (LDM / STM)
    else if ((instr & 0x0E000000) == 0x08000000) {
        executeBlockDataTransfer(instr);
    }
    // 11. Single Data Transfer (LDR / STR)
    else if ((instr & 0x0C000000) == 0x04000000) {
        executeSingleDataTransfer(instr);
    }
    // 12. Data Processing
    else if ((instr & 0x0C000000) == 0x00000000) {
        executeDataProcessing(instr);
    }
    else {
        static int unkCount = 0;
        if (unkCount++ < 10 && debugLogging) {
            std::cerr << "[CPU] Undefined/Unhandled ARM instruction 0x" << std::hex << std::setw(8) << instr
                      << " at PC 0x" << pc << std::dec << std::endl;
        }
        handleUndefinedInstruction(instr);
    }
}

void ARM920T::executeBranch(u32 instr) {
    bool link = (instr & (1 << 24)) != 0;
    i32 offset = instr & 0x00FFFFFF;
    if (offset & 0x00800000) offset |= 0xFF000000;
    offset <<= 2;

    if (link) r[14] = r[15]; // Next instruction address (current PC + 4)
    r[15] = static_cast<u32>(static_cast<i32>(r[15] + 4) + offset);
}

void ARM920T::executeBX(u32 instr) {
    bool link = (instr & (1 << 5)) != 0;
    u32 rm = instr & 0xF;
    u32 target = (rm == 15) ? (r[15] + 4) : r[rm];

    if (link) r[14] = r[15];

    if (target & 1) {
        cpsr |= FLAG_T;
        r[15] = target & ~1;
    } else {
        cpsr &= ~FLAG_T;
        r[15] = target & ~3;
    }
}

void ARM920T::executeMRS(u32 instr) {
    bool isSPSR = (instr & (1 << 22)) != 0;
    u32 rd = (instr >> 12) & 0xF;
    if (rd != 15) {
        r[rd] = isSPSR ? spsr : cpsr;
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

    if (!isSPSR && (cpsr & 0x1F) == 0x10) { // User mode
        mask &= 0xFF000000; // Only condition flags (f) can be modified in User mode
    }

    u32 val = 0;
    if (instr & (1 << 25)) { // Immediate
        u32 imm = instr & 0xFF;
        u32 rot = ((instr >> 8) & 0xF) * 2;
        val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else { // Register
        u32 rm = instr & 0xF;
        val = (rm == 15) ? (r[15] + 4) : r[rm];
    }

    if (isSPSR) {
        spsr = (spsr & ~mask) | (val & mask);
    } else {
        u32 newCpsr = (cpsr & ~mask) | (val & mask);
        if ((mask & 0x1F) && (newCpsr & 0x1F) != (cpsr & 0x1F)) {
            switchMode(newCpsr & 0x1F);
        }
        cpsr = newCpsr;
    }
}

void ARM920T::executeCP15(u32 instr) {
    bool is_mrc = (instr & (1 << 20)) != 0;
    u32 crn = (instr >> 16) & 0xF;
    u32 rd  = (instr >> 12) & 0xF;

    if (is_mrc) {
        u32 val = 0;
        if (crn == 0) val = 0x41009200; // ARM920T (ARMv4T) ID Code
        else if (crn == 1) val = cp15_control;
        else if (crn == 2) val = cp15_ttb;
        else if (crn == 3) val = cp15_dacr;
        else if (crn == 5) val = cp15_fsr;
        else if (crn == 6) val = cp15_far;
        if (rd != 15) r[rd] = val;
    } else {
        u32 val = (rd == 15) ? (r[15] + 4) : r[rd];
        if (crn == 1) {
            cp15_control = val;
            bus.setMmuEnabled((val & 1) != 0);
        } else if (crn == 2) {
            if (debugLogging) std::cout << "[CP15 TTB WRITE] val=0x" << std::hex << val << " PC=0x" << r[15] << std::dec << std::endl;
            cp15_ttb = val;
            bus.setTtb(val);
        } else if (crn == 3) {
            cp15_dacr = val;
            bus.setDacr(val);
        } else if (crn == 5) {
            cp15_fsr = val;
        } else if (crn == 6) {
            cp15_far = val;
        } else if (crn == 8) {
            bus.flushTlb();
        }
        // CRn=7 (Cache flush) is accepted as NOP
    }
}

void ARM920T::executeDataProcessing(u32 instr) {
    bool isImm = (instr & (1 << 25)) != 0;
    u32 opcode = (instr >> 21) & 0xF;
    bool setCond = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;

    u32 op1 = (rn == 15) ? (r[15] + 4) : r[rn];
    u32 op2 = 0;
    bool carry = (cpsr & FLAG_C) != 0;
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
        op2 = (rm == 15) ? (r[15] + 4) : r[rm];
        u32 shiftType = (instr >> 5) & 3;
        bool isRegShift = (instr & (1 << 4)) != 0;
        u32 shiftAmt = 0;
        if (isRegShift) {
            u32 rs = (instr >> 8) & 0xF;
            shiftAmt = r[rs] & 0xFF;
        } else {
            shiftAmt = (instr >> 7) & 0x1F;
        }
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
                if (static_cast<u64>(op1) + op2 + cVal > 0xFFFFFFFFULL) cpsr |= FLAG_C;
                else cpsr &= ~FLAG_C;
                if (((op1 ^ result) & (op2 ^ result) & 0x80000000) != 0) cpsr |= FLAG_V;
                else cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x6: { // SBC
            u32 cVal = arithmeticCarry ? 0 : 1;
            result = op1 - op2 - cVal;
            if (setCond) {
                setNZFlags(result);
                if (static_cast<u64>(op1) >= static_cast<u64>(op2) + cVal) cpsr |= FLAG_C;
                else cpsr &= ~FLAG_C;
                if (((op1 ^ op2) & (op1 ^ result) & 0x80000000) != 0) cpsr |= FLAG_V;
                else cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x7: { // RSC
            u32 cVal = arithmeticCarry ? 0 : 1;
            result = op2 - op1 - cVal;
            if (setCond) {
                setNZFlags(result);
                if (static_cast<u64>(op2) >= static_cast<u64>(op1) + cVal) cpsr |= FLAG_C;
                else cpsr &= ~FLAG_C;
                if (((op2 ^ op1) & (op2 ^ result) & 0x80000000) != 0) cpsr |= FLAG_V;
                else cpsr &= ~FLAG_V;
            }
            break;
        }
        case 0x8: // TST
        case 0x9: // TEQ
            result = opcode == 0x8 ? (op1 & op2) : (op1 ^ op2);
            writeResult = false;
            if (setCond) {
                setNZFlags(result);
                if (carry) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
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
            r[15] = result & ~3;
            if (setCond) {
                u32 newCpsr = spsr;
                if ((newCpsr & 0x1F) != (cpsr & 0x1F)) {
                    switchMode(newCpsr & 0x1F);
                }
                cpsr = newCpsr;
            }
        } else {
            r[rd] = result;
            if (setCond && opcode != 0x2 && opcode != 0x3 && opcode != 0x4 && opcode != 0x5 && opcode != 0x6 && opcode != 0x7) {
                setNZFlags(result);
                if (carry) cpsr |= FLAG_C;
                else cpsr &= ~FLAG_C;
            }
        }
    }
}

void ARM920T::executeSingleDataTransfer(u32 instr) {
    bool isImm = (instr & (1 << 25)) == 0;
    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool isByte = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;

    u32 baseVal = (rn == 15) ? (r[15] + 4) : r[rn];
    u32 offset = 0;

    if (isImm) {
        offset = instr & 0xFFF;
    } else {
        u32 rm = instr & 0xF;
        offset = (rm == 15) ? (r[15] + 4) : r[rm];
        u32 shiftType = (instr >> 5) & 3;
        u32 shiftAmt = (instr >> 7) & 0x1F;
        bool dummyCarry = (cpsr & FLAG_C) != 0;
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
                cpsr |= FLAG_T;
                r[15] = val & ~1;
            } else {
                cpsr &= ~FLAG_T;
                r[15] = val & ~3;
            }
        } else {
            r[rd] = val;
        }
    } else {
        u32 val = (rd == 15) ? (r[15] + 4) : r[rd];
        if (isByte) bus.write8(targetAddr, val & 0xFF);
        else bus.write32(targetAddr & ~3u, val);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
    }

    if (!pre) r[rn] = up ? (baseVal + offset) : (baseVal - offset);
    else if (writeback) r[rn] = targetAddr;
}

void ARM920T::executeHalfwordTransfer(u32 instr) {
    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool isImm = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;
    u32 op = (instr >> 5) & 3;

    u32 baseVal = (rn == 15) ? (r[15] + 4) : r[rn];
    u32 offset = 0;

    if (isImm) {
        offset = ((instr >> 8) & 0xF) << 4 | (instr & 0xF);
    } else {
        u32 rm = instr & 0xF;
        offset = (rm == 15) ? (r[15] + 4) : r[rm];
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
        r[rd] = val;
    } else {
        if (op == 1) bus.write16(targetAddr, r[rd] & 0xFFFF);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
    }

    if (!pre) r[rn] = up ? (baseVal + offset) : (baseVal - offset);
    else if (writeback) r[rn] = targetAddr;
}

void ARM920T::executeBlockDataTransfer(u32 instr) {
    bool pre = (instr & (1 << 24)) != 0;
    bool up = (instr & (1 << 23)) != 0;
    bool sBit = (instr & (1 << 22)) != 0;
    bool writeback = (instr & (1 << 21)) != 0;
    bool isLoad = (instr & (1 << 20)) != 0;
    u32 rn = (instr >> 16) & 0xF;
    u32 regList = instr & 0xFFFF;

    u32 baseVal = (rn == 15) ? (r[15] + 4) : r[rn];
    int count = 0;
    for (int i = 0; i < 16; ++i) if (regList & (1 << i)) count++;

    u32 startAddr = baseVal;
    if (!up) startAddr = baseVal - (count * 4);
    if (pre == up) startAddr += 4;

    bool userBank = sBit && !(regList & (1 << 15));

    u32 currAddr = startAddr;
    for (int i = 0; i < 16; ++i) {
        if (regList & (1 << i)) {
            if (isLoad) {
                u32 val = bus.read32(currAddr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return;
                }
                if (userBank && i == 13) {
                    r13_usr = val;
                } else if (userBank && i == 14) {
                    r14_usr = val;
                } else {
                    r[i] = val;
                }
                if (i == 15) {
                    r[15] &= ~3;
                    if (sBit) {
                        u32 newCpsr = spsr;
                        if ((newCpsr & 0x1F) != (cpsr & 0x1F)) {
                            switchMode(newCpsr & 0x1F);
                        }
                        cpsr = newCpsr;
                    }
                }
            } else {
                u32 val = 0;
                if (userBank && i == 13) val = r13_usr;
                else if (userBank && i == 14) val = r14_usr;
                else val = (i == 15) ? (r[15] + 4) : r[i];
                bus.write32(currAddr, val);
                if (bus.getLastFault() != Bus::MmuFault::NONE) {
                    handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
                    bus.clearLastFault();
                    return;
                }
            }
            currAddr += 4;
        }
    }

    if (writeback && (!isLoad || !(regList & (1 << rn)))) {
        r[rn] = up ? (baseVal + count * 4) : (baseVal - count * 4);
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

    if (!isLong) {
        u32 res = r[rm] * r[rs];
        if (accumulate) res += r[rn];
        r[rd] = res;
        if (setCond) setNZFlags(res);
    } else {
        bool isSigned = (instr & (1 << 22)) != 0;
        u64 res = 0;
        if (isSigned) {
            i64 a = static_cast<i32>(r[rm]);
            i64 b = static_cast<i32>(r[rs]);
            i64 rVal = a * b;
            if (accumulate) rVal += (static_cast<u64>(r[rd]) << 32) | r[rn];
            res = static_cast<u64>(rVal);
        } else {
            res = static_cast<u64>(r[rm]) * static_cast<u64>(r[rs]);
            if (accumulate) res += (static_cast<u64>(r[rd]) << 32) | r[rn];
        }
        r[rn] = res & 0xFFFFFFFF;
        r[rd] = (res >> 32) & 0xFFFFFFFF;
        if (setCond) {
            cpsr &= ~(FLAG_N | FLAG_Z);
            if (res & 0x8000000000000000ULL) cpsr |= FLAG_N;
            if (res == 0) cpsr |= FLAG_Z;
        }
    }
}

void ARM920T::executeSwap(u32 instr) {
    u32 rn = (instr >> 16) & 0xF;
    u32 rd = (instr >> 12) & 0xF;
    u32 rm = instr & 0xF;
    u32 addr = r[rn];
    bool isByte = (instr & (1 << 22)) != 0;

    if (isByte) {
        u8 temp = bus.read8(addr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        bus.write8(addr, r[rm] & 0xFF);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        r[rd] = temp;
    } else {
        u32 temp = readRotatedWord(bus, addr);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        bus.write32(addr & ~3u, r[rm]);
        if (bus.getLastFault() != Bus::MmuFault::NONE) {
            handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault());
            bus.clearLastFault();
            return;
        }
        r[rd] = temp;
    }
}

void ARM920T::executeSWI(u32 instr) {
    u32 swiNum = instr & 0x00FFFFFF;
    u32 retAddr = r[15];
    u32 oldCpsr = cpsr;
    switchMode(0x13); // Supervisor mode
    spsr = oldCpsr;
    cpsr |= FLAG_I;              // Disable IRQ
    cpsr &= ~FLAG_T;             // ARM state
    r[14] = retAddr;             // Save return address
    r[15] = (cp15_control & (1 << 13)) ? 0xFFFF0008 : 0x00000008;

    if (debugLogging && bus.isMmuEnabled()) {
        u32 nr = swiNum & 0x000FFFFF;
        if (nr == 0x0b) { // execve
            std::string fn;
            for (int i = 0; i < 64; ++i) {
                u8 ch = 0;
                if (!bus.peek8(r[0] + i, ch) || ch == 0) break;
                fn += static_cast<char>(ch);
            }
            std::cout << "\n>>> [USERSPACE EXECVE] \"" << fn << "\" <<<\n" << std::endl;
        } else if (nr == 0x05) { // open
            std::string fn;
            for (int i = 0; i < 128; ++i) {
                u8 ch = 0;
                if (!bus.peek8(r[0] + i, ch) || ch == 0) break;
                fn += static_cast<char>(ch);
            }
            std::cout << "[USERSPACE OPEN r0=0x" << std::hex << r[0] << "] \"" << fn << "\" flags=0x" << r[1] << std::dec << std::endl;
        } else if (nr == 0x04) { // write
            std::string out;
            u32 len = std::min(r[2], 128u);
            for (u32 i = 0; i < len; ++i) {
                u8 ch = 0;
                if (!bus.peek8(r[1] + i, ch)) break;
                out += static_cast<char>(ch);
            }
            std::cout << "[USERSPACE WRITE fd=" << r[0] << "] \"" << out << "\"" << std::endl;
        } else if (nr == 0x36) { // ioctl
            std::cout << "[USERSPACE IOCTL] fd=" << r[0] << " cmd=0x" << std::hex << r[1] << " arg=0x" << r[2] << std::dec << std::endl;
        } else if (nr == 0xc0) { // mmap2
            std::cout << "[USERSPACE MMAP2] addr=0x" << std::hex << r[0] << " len=0x" << r[1]
                      << " prot=0x" << r[2] << " flags=0x" << r[3] << " fd=" << std::dec << (int)r[4]
                      << " pgoff=0x" << std::hex << r[5] << std::dec << std::endl;
        } else {
            std::cout << "[SWI] Syscall 0x" << std::hex << swiNum << " called from 0x" << (retAddr - 4)
                      << " -> vector 0x" << r[15] << " r0=0x" << r[0] << " r1=0x" << r[1]
                      << " r2=0x" << r[2] << std::dec << std::endl;
        }
    }
}

void ARM920T::stepThumb(u32 physAddr) {
    u32 instrPC = r[15];
    u16 instr = (physAddr != 0xFFFFFFFF) ? bus.read16Phys(physAddr) : bus.read16(instrPC);
    r[15] += 2;

    // Format 2: Add/subtract (register / 3-bit immediate)
    if ((instr & 0xF800) == 0x1800) {
        bool isImm = (instr & (1 << 10)) != 0;
        bool isSub = (instr & (1 << 9)) != 0;
        u32 rn = (instr >> 6) & 7;
        u32 rs = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 op1 = r[rs];
        u32 op2 = isImm ? rn : r[rn];
        u32 res = isSub ? (op1 - op2) : (op1 + op2);
        r[rd] = res;
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
        u32 val = r[rs];
        u32 res = 0;
        bool carry = (cpsr & FLAG_C) != 0;

        if (subOp == 0) { // LSL
            if (offset == 0) {
                res = val; // MOV Rd, Rs (carry unaffected)
            } else {
                carry = (val >> (32 - offset)) & 1;
                res = val << offset;
                if (carry) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
            }
        } else if (subOp == 1) { // LSR
            if (offset == 0) { // LSR #32
                carry = (val >> 31) & 1;
                res = 0;
            } else {
                carry = (val >> (offset - 1)) & 1;
                res = val >> offset;
            }
            if (carry) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
        } else if (subOp == 2) { // ASR
            if (offset == 0) { // ASR #32
                carry = (val >> 31) & 1;
                res = (val & 0x80000000) ? 0xFFFFFFFF : 0;
            } else {
                carry = (static_cast<i32>(val) >> (offset - 1)) & 1;
                res = static_cast<u32>(static_cast<i32>(val) >> offset);
            }
            if (carry) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
        }
        r[rd] = res;
        setNZFlags(res);
        return;
    }

    // Format 3: Move/compare/add/subtract immediate
    if ((instr & 0xE000) == 0x2000) {
        u32 subOp = (instr >> 11) & 3;
        u32 rd = (instr >> 8) & 7;
        u32 imm = instr & 0xFF;
        if (subOp == 0) { // MOV
            r[rd] = imm;
            setNZFlags(imm);
        } else if (subOp == 1) { // CMP
            setSubFlags(r[rd], imm, r[rd] - imm);
        } else if (subOp == 2) { // ADD
            u32 op1 = r[rd];
            u32 res = op1 + imm;
            r[rd] = res;
            setAddFlags(op1, imm, res);
        } else if (subOp == 3) { // SUB
            u32 op1 = r[rd];
            u32 res = op1 - imm;
            r[rd] = res;
            setSubFlags(op1, imm, res);
        }
        return;
    }

    // Format 4: ALU operations
    if ((instr & 0xFC00) == 0x4000) {
        u32 aluOp = (instr >> 6) & 0xF;
        u32 rs = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 op1 = r[rd];
        u32 op2 = r[rs];
        switch (aluOp) {
            case 0x0: { // AND
                r[rd] = op1 & op2;
                setNZFlags(r[rd]);
                break;
            }
            case 0x1: { // EOR
                r[rd] = op1 ^ op2;
                setNZFlags(r[rd]);
                break;
            }
            case 0x2: { // LSL
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (op1 >> (32 - amt)) & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = op1 << amt;
                } else if (amt == 32) {
                    bool c = op1 & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = 0;
                } else {
                    cpsr &= ~FLAG_C;
                    r[rd] = 0;
                }
                setNZFlags(r[rd]);
                break;
            }
            case 0x3: { // LSR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (op1 >> (amt - 1)) & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = op1 >> amt;
                } else if (amt == 32) {
                    bool c = (op1 >> 31) & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = 0;
                } else {
                    cpsr &= ~FLAG_C;
                    r[rd] = 0;
                }
                setNZFlags(r[rd]);
                break;
            }
            case 0x4: { // ASR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    r[rd] = op1;
                } else if (amt < 32) {
                    bool c = (static_cast<i32>(op1) >> (amt - 1)) & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = static_cast<u32>(static_cast<i32>(op1) >> amt);
                } else {
                    bool c = (op1 >> 31) & 1;
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = (op1 & 0x80000000) ? 0xFFFFFFFF : 0;
                }
                setNZFlags(r[rd]);
                break;
            }
            case 0x5: { // ADC
                u32 c = (cpsr & FLAG_C) ? 1 : 0;
                u32 res = op1 + op2 + c;
                r[rd] = res;
                setNZFlags(res);
                if (static_cast<u64>(op1) + op2 + c > 0xFFFFFFFFULL) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                if (((op1 ^ res) & (op2 ^ res) & 0x80000000) != 0) cpsr |= FLAG_V; else cpsr &= ~FLAG_V;
                break;
            }
            case 0x6: { // SBC
                u32 notC = (cpsr & FLAG_C) ? 0 : 1;
                u32 res = op1 - op2 - notC;
                r[rd] = res;
                setNZFlags(res);
                if (static_cast<u64>(op1) >= static_cast<u64>(op2) + notC) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                if (((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0) cpsr |= FLAG_V; else cpsr &= ~FLAG_V;
                break;
            }
            case 0x7: { // ROR
                u32 amt = op2 & 0xFF;
                if (amt == 0) {
                    r[rd] = op1;
                } else {
                    u32 amt5 = amt & 0x1F;
                    bool c = (amt5 == 0) ? ((op1 >> 31) & 1) : ((op1 >> (amt5 - 1)) & 1);
                    if (c) cpsr |= FLAG_C; else cpsr &= ~FLAG_C;
                    r[rd] = (amt5 == 0) ? op1 : ((op1 >> amt5) | (op1 << (32 - amt5)));
                }
                setNZFlags(r[rd]);
                break;
            }
            case 0x8: { // TST
                setNZFlags(op1 & op2);
                break;
            }
            case 0x9: { // NEG
                u32 res = 0 - op2;
                r[rd] = res;
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
                r[rd] = op1 | op2;
                setNZFlags(r[rd]);
                break;
            }
            case 0xD: { // MUL
                r[rd] = op1 * op2;
                setNZFlags(r[rd]);
                break;
            }
            case 0xE: { // BIC
                r[rd] = op1 & ~op2;
                setNZFlags(r[rd]);
                break;
            }
            case 0xF: { // MVN
                r[rd] = ~op2;
                setNZFlags(r[rd]);
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
        u32 valS = (s == 15) ? (instrPC + 4) : r[s];
        u32 valD = (d == 15) ? (instrPC + 4) : r[d];
        if (op5 == 0) { // ADD
            u32 res = valD + valS;
            if (d == 15) r[15] = res & ~1;
            else r[d] = res;
        } else if (op5 == 1) { // CMP
            setSubFlags(valD, valS, valD - valS);
        } else if (op5 == 2) { // MOV
            if (d == 15) r[15] = valS & ~1;
            else r[d] = valS;
        } else if (op5 == 3) { // BX / BLX
            if (instr & 0x80) { // BLX
                r[14] = (instrPC + 2) | 1;
            }
            if (valS & 1) {
                cpsr |= FLAG_T;
                r[15] = valS & ~1;
            } else {
                cpsr &= ~FLAG_T;
                r[15] = valS & ~3;
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
        r[rd] = val;
        return;
    }

    // Format 7 & Format 8: Load/store register offset / sign-extended byte/halfword
    if ((instr & 0xF000) == 0x5000) {
        bool signedOrHalfword = (instr & (1 << 9)) != 0;
        u32 ro = (instr >> 6) & 7;
        u32 rb = (instr >> 3) & 7;
        u32 rd = instr & 7;
        u32 addr = r[rb] + r[ro];
        if (!signedOrHalfword) { // Format 7: Load/store with register offset
            bool load = (instr & (1 << 11)) != 0;
            bool isByte = (instr & (1 << 10)) != 0;
            if (load) {
                if (isByte) {
                    u8 val = bus.read8(addr);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                    r[rd] = val;
                } else {
                    u32 val = readRotatedWord(bus, addr);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                    r[rd] = val;
                }
            } else {
                if (isByte) {
                    bus.write8(addr, r[rd] & 0xFF);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                } else {
                    bus.write32(addr & ~3u, r[rd]);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                }
            }
        } else { // Format 8: Load/store sign-extended byte/halfword
            u32 op8 = (instr >> 10) & 3;
            if (op8 == 0) { // STRH
                bus.write16(addr, r[rd] & 0xFFFF);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            } else if (op8 == 1) { // LDSB
                u8 val = bus.read8(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                r[rd] = static_cast<u32>(static_cast<i8>(val));
            } else if (op8 == 2) { // LDRH
                u16 val = bus.read16(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                r[rd] = val;
            } else if (op8 == 3) { // LDSH
                u16 val = bus.read16(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                r[rd] = static_cast<u32>(static_cast<i16>(val));
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
        u32 addr = r[rb] + (isByte ? offset5 : (offset5 * 4));
        if (load) {
            if (isByte) {
                u8 val = bus.read8(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                r[rd] = val;
            } else {
                u32 val = readRotatedWord(bus, addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                r[rd] = val;
            }
        } else {
            if (isByte) {
                bus.write8(addr, r[rd] & 0xFF);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            } else {
                bus.write32(addr & ~3u, r[rd]);
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
        u32 addr = r[rb] + (offset5 * 2);
        if (load) {
            u16 val = bus.read16(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            r[rd] = val;
        } else {
            bus.write16(addr, r[rd] & 0xFFFF);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }

    // Format 11: SP-relative load/store
    if ((instr & 0xF000) == 0x9000) {
        bool load = (instr & (1 << 11)) != 0;
        u32 rd = (instr >> 8) & 7;
        u32 imm = (instr & 0xFF) * 4;
        u32 addr = r[13] + imm;
        if (load) {
            u32 val = bus.read32(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            r[rd] = val;
        } else {
            bus.write32(addr, r[rd]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }

    // Format 12: Load address
    if ((instr & 0xF000) == 0xA000) {
        bool sp = (instr & (1 << 11)) != 0;
        u32 rd = (instr >> 8) & 7;
        u32 imm = (instr & 0xFF) * 4;
        if (sp) r[rd] = r[13] + imm;
        else r[rd] = ((instrPC + 4) & ~3) + imm;
        return;
    }

    // Format 13: Add offset to Stack Pointer
    if ((instr & 0xFF00) == 0xB000) {
        bool sub = (instr & (1 << 7)) != 0;
        u32 imm = (instr & 0x7F) * 4;
        if (sub) r[13] -= imm;
        else r[13] += imm;
        return;
    }

    // Format 14: Push/Pop registers
    if ((instr & 0xFE00) == 0xB400) { // PUSH
        bool rBit = (instr & (1 << 8)) != 0; // LR
        u32 count = 0;
        for (int i = 0; i < 8; ++i) if (instr & (1 << i)) count++;
        if (rBit) count++;
        u32 addr = r[13] - count * 4;
        r[13] = addr;
        for (int i = 0; i < 8; ++i) {
            if (instr & (1 << i)) {
                bus.write32(addr, r[i]);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                addr += 4;
            }
        }
        if (rBit) {
            bus.write32(addr, r[14]);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
        }
        return;
    }
    if ((instr & 0xFE00) == 0xBC00) { // POP
        bool rBit = (instr & (1 << 8)) != 0; // PC
        u32 addr = r[13];
        for (int i = 0; i < 8; ++i) {
            if (instr & (1 << i)) {
                r[i] = bus.read32(addr);
                if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                addr += 4;
            }
        }
        if (rBit) {
            u32 target = bus.read32(addr);
            if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
            addr += 4;
            if (target & 1) {
                cpsr |= FLAG_T;
                r[15] = target & ~1;
            } else {
                cpsr &= ~FLAG_T;
                r[15] = target & ~3;
            }
        }
        r[13] = addr;
        return;
    }

    // Format 15: Multiple load/store
    if ((instr & 0xF000) == 0xC000) {
        bool load = (instr & (1 << 11)) != 0;
        u32 rb = (instr >> 8) & 7;
        u32 addr = r[rb];
        for (int i = 0; i < 8; ++i) {
            if (instr & (1 << i)) {
                if (load) {
                    r[i] = bus.read32(addr);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                } else {
                    bus.write32(addr, r[i]);
                    if (bus.getLastFault() != Bus::MmuFault::NONE) { handleDataAbort(bus.getLastFaultAddr(), bus.getLastFault()); bus.clearLastFault(); return; }
                }
                addr += 4;
            }
        }
        if (!load || !(instr & (1 << rb))) {
            r[rb] = addr;
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
                i32 offset = static_cast<i32>(static_cast<i8>(instr & 0xFF)) * 2;
                r[15] = (instrPC + 4) + offset;
            }
        }
        return;
    }

    // Format 18: Unconditional branch
    if ((instr & 0xF800) == 0xE000) {
        i32 off = instr & 0x7FF;
        if (off & 0x400) off |= ~0x7FF;
        r[15] = (instrPC + 4) + (off * 2);
        return;
    }

    // Format 19: Long branch with link / BLX
    if ((instr & 0xE000) == 0xE000) {
        u32 op19 = (instr >> 11) & 0x1F;
        if (op19 == 0x1E) { // BL prefix
            i32 off = instr & 0x7FF;
            if (off & 0x400) off |= ~0x7FF;
            r[14] = (instrPC + 4) + (off << 12);
            return;
        } else if (op19 == 0x1F) { // BL suffix
            u32 target = r[14] + ((instr & 0x7FF) << 1);
            r[14] = r[15] | 1;
            r[15] = target;
            return;
        } else if (op19 == 0x1D) { // BLX suffix
            u32 target = (r[14] + ((instr & 0x7FF) << 1)) & ~3;
            r[14] = r[15] | 1;
            cpsr &= ~FLAG_T;
            r[15] = target;
            return;
        }
    }
}

std::string ARM920T::disassembleCurrentARM() const {
    std::stringstream ss;
    u32 instr = 0;
    if (bus.peek32(r[15], instr)) {
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << r[15] << ": "
           << std::setw(8) << instr;
    } else {
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << r[15] << ": [unmapped]";
    }
    return ss.str();
}

} // namespace oceanblast
