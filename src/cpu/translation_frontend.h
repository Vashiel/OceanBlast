#pragma once
#include "translation_ir.h"

namespace oceanblast::translation {

inline void constantShift(IntegerOperation& op, unsigned kind, unsigned amount) {
    op.shift=static_cast<Shift>(kind);
    op.shiftAmount=uint8_t(amount);
    if (kind==0 && amount==0) { op.shifterCarry=CarrySource::Preserve; return; }
    op.shifterCarry=CarrySource::Shifter;
    if (!amount) {
        if (kind==1 || kind==2) op.shiftAmount=32;
        else if (kind==3) { op.shift=Shift::Rrx; op.shiftAmount=1; }
    }
}

inline bool decodeArmInteger(uint32_t raw, IntegerOperation& op) {
    // ARMv4T data processing with an immediate or a constant register shift.
    // Register-controlled shifts and PC operands require later lowering.
    if ((raw>>28)==15 || (raw&0x0c000000u)!=0) return false;
    const bool immediate=(raw&(1u<<25))!=0;
    if (!immediate && (raw&0x10u)) return false;
    const unsigned code=(raw>>21)&15, rn=(raw>>16)&15, rd=(raw>>12)&15;
    const bool setFlags=(raw&(1u<<20))!=0;
    const bool compare=code>=8 && code<=11;
    if (compare && !setFlags) return false; // Includes PSR/control encodings.
    const bool usesLeft=code!=13 && code!=15;
    if ((usesLeft && rn==15) || (!compare && rd==15) || (!immediate && (raw&15)==15)) return false;
    static constexpr IntegerOpcode codes[16]={
        IntegerOpcode::And,IntegerOpcode::Xor,IntegerOpcode::Subtract,IntegerOpcode::ReverseSubtract,
        IntegerOpcode::Add,IntegerOpcode::AddCarry,IntegerOpcode::SubtractCarry,IntegerOpcode::ReverseSubtractCarry,
        IntegerOpcode::And,IntegerOpcode::Xor,IntegerOpcode::Subtract,IntegerOpcode::Add,
        IntegerOpcode::Or,IntegerOpcode::Move,IntegerOpcode::BitClear,IntegerOpcode::Not
    };
    op={}; op.opcode=codes[code];
    if (usesLeft) op.left=Value::reg(uint8_t(rn));
    if (!compare) op.destination=uint8_t(rd);
    const bool arithmetic=(code>=2 && code<=7) || code==10 || code==11;
    op.flags=setFlags ? (arithmetic?FlagWrite::Arithmetic:FlagWrite::Logical) : FlagWrite::Preserve;
    if (immediate) {
        const unsigned rotate=((raw>>8)&15)*2;
        const uint32_t value=raw&255;
        const uint32_t rotated=rotate ? (value>>rotate)|(value<<(32-rotate)) : value;
        op.right=Value::constant(rotated);
        op.shifterCarry=rotate ? ((rotated>>31)?CarrySource::One:CarrySource::Zero) : CarrySource::Preserve;
    } else {
        op.right=Value::reg(uint8_t(raw&15));
        constantShift(op,(raw>>5)&3,(raw>>7)&31);
    }
    return true;
}

inline bool decodeThumbInteger(uint16_t raw, IntegerOperation& op) {
    op={};
    if ((raw&0xe000u)==0x2000u) {
        const unsigned code=(raw>>11)&3;
        const uint8_t rd=uint8_t((raw>>8)&7);
        op.right=Value::constant(raw&255);
        if (code!=0) op.left=Value::reg(rd);
        if (code!=1) op.destination=rd;
        op.opcode=code==0?IntegerOpcode::Move:(code==2?IntegerOpcode::Add:IntegerOpcode::Subtract);
        op.flags=code==0?FlagWrite::Logical:FlagWrite::Arithmetic;
        return true;
    }
    if ((raw&0xf800u)==0x1800u) {
        op.opcode=(raw&0x200u)?IntegerOpcode::Subtract:IntegerOpcode::Add;
        op.left=Value::reg(uint8_t((raw>>3)&7));
        op.right=(raw&0x400u)?Value::constant((raw>>6)&7):Value::reg(uint8_t((raw>>6)&7));
        op.destination=uint8_t(raw&7); op.flags=FlagWrite::Arithmetic;
        return true;
    }
    if ((raw&0xe000u)==0) {
        op.right=Value::reg(uint8_t((raw>>3)&7)); op.destination=uint8_t(raw&7);
        op.flags=FlagWrite::Logical;
        constantShift(op,(raw>>11)&3,(raw>>6)&31);
        return true;
    }
    if ((raw&0xfc00u)==0x4000u) {
        const unsigned code=(raw>>6)&15;
        const uint8_t rd=uint8_t(raw&7),rs=uint8_t((raw>>3)&7);
        op.left=Value::reg(rd); op.right=Value::reg(rs); op.destination=rd;
        op.flags=FlagWrite::Logical;
        switch(code) {
            case 0: op.opcode=IntegerOpcode::And; break;
            case 1: op.opcode=IntegerOpcode::Xor; break;
            case 5: op.opcode=IntegerOpcode::AddCarry; op.flags=FlagWrite::Arithmetic; break;
            case 6: op.opcode=IntegerOpcode::SubtractCarry; op.flags=FlagWrite::Arithmetic; break;
            case 8: op.opcode=IntegerOpcode::And; op.destination.reset(); break;
            case 9: op.opcode=IntegerOpcode::Subtract; op.left=Value::constant(0); op.flags=FlagWrite::Arithmetic; break;
            case 10: op.opcode=IntegerOpcode::Subtract; op.destination.reset(); op.flags=FlagWrite::Arithmetic; break;
            case 11: op.opcode=IntegerOpcode::Add; op.destination.reset(); op.flags=FlagWrite::Arithmetic; break;
            case 12: op.opcode=IntegerOpcode::Or; break;
            case 14: op.opcode=IntegerOpcode::BitClear; break;
            case 15: op.opcode=IntegerOpcode::Not; op.left=Value::constant(0); break;
            default: return false;
        }
        return true;
    }
    return false;
}

inline bool isControlFlow(uint32_t raw,InstructionSet set) {
    if (set==InstructionSet::Arm)
        return (raw&0x0e000000u)==0x0a000000u || (raw&0x0ffffff0u)==0x012fff10u;
    return (raw&0xf000u)==0xd000u || (raw&0xf800u)==0xe000u ||
           (raw&0xf000u)==0xf000u || (raw&0xff00u)==0x4700u;
}

// Fetch must be side-effect-free and return unavailable for unsafe/unmapped
// addresses. This frontend never reads the machine bus or executes a fallback.
// The initial 1 KB span also stays within ARM small-page permission subregions.
template<class Fetch>
Block decodeBlock(uint32_t pc,InstructionSet set,Fetch&& fetch,BlockLimits limits={}) {
    Block block{set,pc,pc,BlockStop::InstructionLimit,std::nullopt,{}};
    const uint32_t bytes=set==InstructionSet::Arm?4:2;
    if (pc&(bytes-1)) { block.stop=BlockStop::MisalignedEntry; return block; }
    uint32_t consumed=0;
    for (;;) {
        if (block.instructions.size()>=limits.instructions) { block.stop=BlockStop::InstructionLimit; break; }
        if (bytes>limits.bytes-consumed) { block.stop=BlockStop::ByteLimit; break; }
        if ((pc^block.entryPc)&~1023u) { block.stop=BlockStop::PageBoundary; break; }
        const FetchResult word=fetch(pc,set);
        if (!word.available) { block.stop=BlockStop::FetchUnavailable; break; }
        const uint32_t raw=set==InstructionSet::Arm?word.encoding:(word.encoding&0xffffu);
        IntegerOperation op;
        const bool decoded=set==InstructionSet::Arm?decodeArmInteger(raw,op):decodeThumbInteger(uint16_t(raw),op);
        if (!decoded) {
            block.stop=isControlFlow(raw,set)?BlockStop::ControlFlow:BlockStop::Unsupported;
            block.stopEncoding=raw;
            break;
        }
        block.instructions.push_back({pc,raw,uint8_t(bytes),set==InstructionSet::Arm?static_cast<Condition>(raw>>28):Condition::Always,op});
        consumed+=bytes;
        const uint32_t next=pc+bytes;
        if (next<pc) { pc=next; block.stop=BlockStop::AddressWrap; break; }
        pc=next;
    }
    block.stopPc=pc;
    return block;
}

} // namespace oceanblast::translation
