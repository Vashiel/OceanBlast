#include "arm920t.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>

namespace oceanblast {

ARM920T::ARM920T(Bus& bus) : bus(bus), cpsr(0x00000013), spsr(0), halted(false) {
    reset();
}

ARM920T::~ARM920T() {}

void ARM920T::reset(u32 startAddress) {
    std::memset(r, 0, sizeof(r));
    r[15] = startAddress; // Reset vector (typically 0x00000000 in Steppingstone SRAM)
    r[13] = 0x00000F00;   // Boot stack pointer in Steppingstone SRAM
    cpsr  = 0x00000013;   // Supervisor (SVC32) mode, ARM state, IRQ/FIQ disabled
    spsr  = 0;
    halted = false;

    cp15_control = 0x00000070;
    cp15_ttb     = 0;
    cp15_dacr    = 0;
    bus.setMmuEnabled(false);
    bus.setTtb(0);
    bus.setDacr(0);
}

void ARM920T::dumpState() const {
    std::cout << "[CPU] PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << r[15]
              << " SP: 0x" << std::setw(8) << r[13]
              << " LR: 0x" << std::setw(8) << r[14]
              << " CPSR: 0x" << std::setw(8) << cpsr
              << (isThumb() ? " (Thumb)" : " (ARM)") << std::dec << std::endl;
    for (int i = 0; i < 12; i += 4) {
        std::cout << "  r" << i << ": 0x" << std::hex << std::setw(8) << r[i]
                  << "  r" << (i+1) << ": 0x" << std::setw(8) << r[i+1]
                  << "  r" << (i+2) << ": 0x" << std::setw(8) << r[i+2]
                  << "  r" << (i+3) << ": 0x" << std::setw(8) << r[i+3] << std::dec << std::endl;
    }
}

void ARM920T::step() {
    if (halted) return;
    if (isThumb()) {
        stepThumb();
    } else {
        stepARM();
    }
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

u32 ARM920T::shiftOperand(u32 val, u32 type, u32 amount, bool& carryOut) {
    carryOut = (cpsr & FLAG_C) != 0;
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
            if (amount == 0) return val;
            carryOut = (val >> (amount - 1)) & 1;
            return (val >> amount) | (val << (32 - amount));
        default:
            return val;
    }
}

void ARM920T::stepARM() {
    u32 pc = r[15];
    u32 instr = bus.read32(pc);
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
    // 5. Multiply
    else if ((instr & 0x0E000090) == 0x00000090 && ((instr & 0x00000060) == 0)) {
        executeMultiply(instr);
    }
    // 6. Halfword Data Transfer
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
        cpsr = (cpsr & ~mask) | (val & mask);
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
        if (rd != 15) r[rd] = val;
    } else {
        u32 val = (rd == 15) ? (r[15] + 4) : r[rd];
        if (crn == 1) {
            cp15_control = val;
            bus.setMmuEnabled((val & 1) != 0);
        } else if (crn == 2) {
            cp15_ttb = val;
            bus.setTtb(val);
        } else if (crn == 3) {
            cp15_dacr = val;
            bus.setDacr(val);
        }
        // CRn=7 (Cache flush) and CRn=8 (TLB flush) are accepted as NOPs
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
        op2 = shiftOperand(op2, shiftType, shiftAmt, carry);
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
            u32 cVal = carry ? 1 : 0;
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
            u32 cVal = carry ? 0 : 1;
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
            u32 cVal = carry ? 0 : 1;
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
        case 0x8: result = op1 & op2; writeResult = false; if (setCond) setNZFlags(result); break; // TST
        case 0x9: result = op1 ^ op2; writeResult = false; if (setCond) setNZFlags(result); break; // TEQ
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
            if (setCond) cpsr = spsr;
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
        bool dummyCarry = false;
        offset = shiftOperand(offset, shiftType, shiftAmt, dummyCarry);
    }

    u32 targetAddr = pre ? (up ? (baseVal + offset) : (baseVal - offset)) : baseVal;

    if (isLoad) {
        u32 val = isByte ? bus.read8(targetAddr) : bus.read32(targetAddr);
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
        else bus.write32(targetAddr, val);
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
        r[rd] = val;
    } else {
        if (op == 1) bus.write16(targetAddr, r[rd] & 0xFFFF);
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

    u32 currAddr = startAddr;
    for (int i = 0; i < 16; ++i) {
        if (regList & (1 << i)) {
            if (isLoad) {
                u32 val = bus.read32(currAddr);
                r[i] = val;
                if (i == 15) {
                    r[15] &= ~3;
                    if (sBit) cpsr = spsr;
                }
            } else {
                u32 val = (i == 15) ? (r[15] + 4) : r[i];
                bus.write32(currAddr, val);
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

void ARM920T::executeSWI(u32 instr) {
    (void)instr;
    // Software interrupt / supervisor call
    spsr = cpsr;
    cpsr = (cpsr & ~0x1F) | 0x13; // Supervisor mode
    cpsr |= FLAG_I;              // Disable IRQ
    r[14] = r[15];               // Save return address
    r[15] = 0x00000008;          // SWI vector
}

void ARM920T::stepThumb() {
    // Thumb instruction decoder
    u32 pc = r[15];
    u16 instr = bus.read16(pc);
    r[15] += 2;

    u32 op = instr >> 13;
    switch (op) {
        case 0: { // Shift by immediate or add/subtract
            u32 subOp = (instr >> 11) & 3;
            u32 rd = instr & 7;
            u32 rs = (instr >> 3) & 7;
            u32 offset = (instr >> 6) & 0x1F;
            bool dummy = false;
            if (subOp == 3) { // Add/subtract
                bool isImm = (instr & (1 << 10)) != 0;
                bool isSub = (instr & (1 << 9)) != 0;
                u32 rn = (instr >> 6) & 7;
                u32 val = isImm ? rn : r[rn];
                u32 res = isSub ? (r[rs] - val) : (r[rs] + val);
                r[rd] = res;
                if (isSub) setSubFlags(r[rs], val, res);
                else setAddFlags(r[rs], val, res);
            } else {
                r[rd] = shiftOperand(r[rs], subOp, offset, dummy);
                setNZFlags(r[rd]);
            }
            break;
        }
        case 1: { // Move/compare/add/subtract immediate
            u32 subOp = (instr >> 11) & 3;
            u32 rd = (instr >> 8) & 7;
            u32 imm = instr & 0xFF;
            if (subOp == 0) { r[rd] = imm; setNZFlags(imm); }
            else if (subOp == 1) { setSubFlags(r[rd], imm, r[rd] - imm); }
            else if (subOp == 2) { setAddFlags(r[rd], imm, r[rd] + imm); r[rd] += imm; }
            else if (subOp == 3) { setSubFlags(r[rd], imm, r[rd] - imm); r[rd] -= imm; }
            break;
        }
        default:
            break;
    }
}

std::string ARM920T::disassembleCurrentARM() const {
    std::stringstream ss;
    u32 instr = bus.read32(r[15]);
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << r[15] << ": "
       << std::setw(8) << instr;
    return ss.str();
}

} // namespace oceanblast
