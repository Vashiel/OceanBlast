#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace oceanblast::translation {

enum class InstructionSet { Arm, Thumb };
enum class Condition : uint8_t { Eq, Ne, Cs, Cc, Mi, Pl, Vs, Vc, Hi, Ls, Ge, Lt, Gt, Le, Always };
enum class IntegerOpcode { And, Xor, Subtract, ReverseSubtract, Add, AddCarry,
                           SubtractCarry, ReverseSubtractCarry, Or, Move, BitClear, Not, Multiply };
enum class ValueKind { Constant, Register };
struct Value {
    ValueKind kind;
    uint32_t value;
    static Value constant(uint32_t value) { return {ValueKind::Constant,value}; }
    static Value reg(uint8_t index) { return {ValueKind::Register,index}; }
};
enum class Shift { Lsl, Lsr, Asr, Ror, Rrx };
enum class CarrySource { Preserve, Zero, One, Shifter };
enum class FlagWrite { Preserve, Logical, Arithmetic };

// A 32-bit integer operation. Carry-in comes from guest CPSR when required.
// Logical flag writes preserve V; arithmetic flag writes derive NZCV from the
// opcode and its operands. Tests/comparisons have no destination register.
struct IntegerOperation {
    IntegerOpcode opcode = IntegerOpcode::Move;
    Value left = Value::constant(0);
    Value right = Value::constant(0);
    Shift shift = Shift::Lsl;
    uint8_t shiftAmount = 0;
    CarrySource shifterCarry = CarrySource::Preserve;
    FlagWrite flags = FlagWrite::Preserve;
    std::optional<uint8_t> destination;
    std::optional<uint8_t> addend=std::nullopt;
    std::optional<uint8_t> shiftRegister=std::nullopt;
};
struct Instruction {
    uint32_t pc;
    uint32_t encoding;
    uint8_t bytes;
    Condition condition;
    IntegerOperation operation;
    struct MemoryOperation {
        uint8_t base,destination;
        uint32_t offset;
        bool load,byte,pre,up,writeback;
        std::optional<uint8_t> offsetRegister=std::nullopt;
        Shift offsetShift=Shift::Lsl;
        uint8_t offsetShiftAmount=0;
        bool halfword=false,signExtend=false;
    };
    std::optional<MemoryOperation> memory;
    struct BranchOperation { uint32_t target; bool link; };
    std::optional<BranchOperation> branch;
};
enum class BlockStop { Unsupported, ControlFlow, InstructionLimit, ByteLimit,
                       PageBoundary, FetchUnavailable, MisalignedEntry, AddressWrap, MemoryBoundary };
struct Block {
    InstructionSet instructionSet;
    uint32_t entryPc;
    uint32_t stopPc;
    BlockStop stop = BlockStop::InstructionLimit;
    std::optional<uint32_t> stopEncoding;
    std::vector<Instruction> instructions;
};
struct BlockLimits {
    uint32_t instructions = 32;
    uint32_t bytes = 128;
    bool followDirectBranches = false;
};
struct FetchResult {
    bool available;
    uint32_t encoding;
};

} // namespace oceanblast::translation
