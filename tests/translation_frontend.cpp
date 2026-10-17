#include "cpu/translation_frontend.h"
#include "core/machine.h"
#ifdef OCEANBLAST_TEST_NATIVE
#include "cpu/x64_execution_engine.h"
#endif
#include <array>
#include <iostream>
#include <limits>
using namespace oceanblast;
using namespace oceanblast::translation;
static int failures=0;
static void check(const char* name,bool ok) {
    std::cout << (ok?"PASS ":"FAIL ") << name << '\n'; failures+=!ok;
}

// Test-only IR semantics oracle, never linked into the emulator or used as an
// execution backend. Wide arithmetic makes carry/overflow expectations explicit.
static bool conditionHolds(Condition condition,uint32_t flags) {
    const bool n=flags&FLAG_N,z=flags&FLAG_Z,c=flags&FLAG_C,v=flags&FLAG_V;
    switch(condition) {
        case Condition::Eq:return z; case Condition::Ne:return !z;
        case Condition::Cs:return c; case Condition::Cc:return !c;
        case Condition::Mi:return n; case Condition::Pl:return !n;
        case Condition::Vs:return v; case Condition::Vc:return !v;
        case Condition::Hi:return c&&!z; case Condition::Ls:return !c||z;
        case Condition::Ge:return n==v; case Condition::Lt:return n!=v;
        case Condition::Gt:return !z&&n==v; case Condition::Le:return z||n!=v;
        case Condition::Always:return true;
    }
    return false;
}
static void evaluate(const Instruction& instruction,ARM920TState& state) {
    state.r[15]+=instruction.bytes;
    if (!conditionHolds(instruction.condition,state.cpsr)) return;
    const auto& op=instruction.operation;
    auto value=[&](Value operand) { return operand.kind==ValueKind::Register?state.r[operand.value]:operand.value; };
    const bool carryIn=(state.cpsr&FLAG_C)!=0;
    bool carry=carryIn;
    uint32_t a=value(op.left),b=value(op.right);
    const unsigned amount=op.shiftAmount;
    if (op.shifterCarry==CarrySource::Zero) carry=false;
    if (op.shifterCarry==CarrySource::One) carry=true;
    if (op.shifterCarry==CarrySource::Shifter) {
        switch(op.shift) {
            case Shift::Lsl: carry=(b>>(32-amount))&1; b<<=amount; break;
            case Shift::Lsr: carry=(b>>(amount-1))&1; b=amount==32?0:b>>amount; break;
            case Shift::Asr: carry=(b>>(amount-1))&1;
                b=amount==32?((b&FLAG_N)?UINT32_MAX:0):uint32_t(int32_t(b)>>amount); break;
            case Shift::Ror: carry=(b>>(amount-1))&1; b=(b>>amount)|(b<<(32-amount)); break;
            case Shift::Rrx: carry=b&1; b=(uint32_t(carryIn)<<31)|(b>>1); break;
        }
    }
    uint32_t result=0; bool overflow=false;
    auto add=[&](uint32_t left,uint32_t right,uint32_t extra) {
        const uint64_t wide=uint64_t(left)+right+extra;
        const int64_t signedWide=int64_t(int32_t(left))+int64_t(int32_t(right))+extra;
        carry=(wide>>32)!=0;
        overflow=signedWide<std::numeric_limits<int32_t>::min() || signedWide>std::numeric_limits<int32_t>::max();
        return uint32_t(wide);
    };
    auto sub=[&](uint32_t left,uint32_t right,uint32_t borrow) {
        const int64_t signedWide=int64_t(int32_t(left))-int64_t(int32_t(right))-borrow;
        carry=uint64_t(left)>=uint64_t(right)+borrow;
        overflow=signedWide<std::numeric_limits<int32_t>::min() || signedWide>std::numeric_limits<int32_t>::max();
        return uint32_t(uint64_t(left)-right-borrow);
    };
    switch(op.opcode) {
        case IntegerOpcode::And:result=a&b;break; case IntegerOpcode::Xor:result=a^b;break;
        case IntegerOpcode::Or:result=a|b;break; case IntegerOpcode::Move:result=b;break;
        case IntegerOpcode::BitClear:result=a&~b;break; case IntegerOpcode::Not:result=~b;break;
        case IntegerOpcode::Add:result=add(a,b,0);break;
        case IntegerOpcode::AddCarry:result=add(a,b,carryIn);break;
        case IntegerOpcode::Subtract:result=sub(a,b,0);break;
        case IntegerOpcode::ReverseSubtract:result=sub(b,a,0);break;
        case IntegerOpcode::SubtractCarry:result=sub(a,b,!carryIn);break;
        case IntegerOpcode::ReverseSubtractCarry:result=sub(b,a,!carryIn);break;
    }
    if (op.destination) state.r[*op.destination]=result;
    if (op.flags!=FlagWrite::Preserve) {
        state.cpsr&=~(FLAG_N|FLAG_Z|FLAG_C);
        if (result&FLAG_N) state.cpsr|=FLAG_N;
        if (!result) state.cpsr|=FLAG_Z;
        if (carry) state.cpsr|=FLAG_C;
        if (op.flags==FlagWrite::Arithmetic) {
            state.cpsr&=~FLAG_V; if (overflow) state.cpsr|=FLAG_V;
        }
    }
}
int main() {
    const auto arm=InstructionSet::Arm,thumb=InstructionSet::Thumb;
    {
        const std::array<uint32_t,3> words={0xe2800001,0xe3a01002,0xeafffffc};
        unsigned fetches=0;
        auto block=decodeBlock(0,arm,[&](uint32_t pc,InstructionSet) {++fetches; return FetchResult{pc/4<words.size(),pc/4<words.size()?words[pc/4]:0};});
        check("ARM block stops before branch and preserves fallback encoding",
            block.instructions.size()==2 && block.stop==BlockStop::ControlFlow && block.stopPc==8 && block.stopEncoding==words[2] && fetches==3);
        auto capped=decodeBlock(0,arm,[&](uint32_t pc,InstructionSet) {return FetchResult{true,words[pc/4]};},{1,128});
        check("Instruction limit stops without fetching the following instruction",capped.instructions.size()==1 && capped.stop==BlockStop::InstructionLimit && capped.stopPc==4);
    }
    auto constant=[](uint32_t,InstructionSet) { return FetchResult{true,0xe2800001}; };
    check("ARM misalignment is rejected before fetch",decodeBlock(2,arm,constant).stop==BlockStop::MisalignedEntry);
    check("Guest byte limit bounds decoding",decodeBlock(0,arm,constant,{32,7}).instructions.size()==1);
    check("Block remains inside a 1 KB permission span",decodeBlock(1020,arm,constant).stop==BlockStop::PageBoundary);
    check("Address wrap produces a bounded exit",decodeBlock(0xfffffffc,arm,constant).stop==BlockStop::AddressWrap);
    check("Unavailable fetch does not produce an instruction",decodeBlock(0,arm,[](uint32_t,InstructionSet){return FetchResult{false,0};}).instructions.empty());
    for (uint32_t raw : {0xe5900000u,0xee070f90u,0xe121f000u,0xe0800211u,0xe1a0f000u,0xe08f0001u,0xf2800001u}) {
        auto block=decodeBlock(0,arm,[&](uint32_t,InstructionSet){return FetchResult{true,raw};});
        check("Unsupported memory control PC or register-shift operation exits before execution",
            block.instructions.empty() && block.stopPc==0 && block.stopEncoding==raw);
    }
    Machine machine;
#ifdef OCEANBLAST_TEST_NATIVE
    X64ExecutionEngine native(machine.state(),machine.cpu(),machine.bus());
    machine.execution().selectEngine(native);
#endif
    size_t comparisons=0; bool equal=true;
    auto compare=[&](uint32_t raw,InstructionSet set,uint32_t pattern,uint32_t flags) {
        auto block=decodeBlock(0,set,[&](uint32_t,InstructionSet){return FetchResult{true,raw};},{1,4});
        if (block.instructions.size()!=1) { equal=false; return; }
        ARM920TState input{};
        input.cpsr=0xd3u|flags|(set==thumb?FLAG_T:0);
        for (unsigned reg=0;reg<15;++reg) input.r[reg]=pattern ^ (reg==1?0x80000000u:0u);
        ARM920TState expected=input;
        evaluate(block.instructions[0],expected);
        machine.state()=input;
        if (set==arm) machine.bus().write32(0,raw); else machine.bus().write16(0,uint16_t(raw));
#ifdef OCEANBLAST_TEST_NATIVE
        machine.execution().run(1);
#else
        machine.cpu().step(0);
#endif
        bool match=expected.cpsr==machine.state().cpsr;
        for (unsigned reg=0;reg<16;++reg) match=match && expected.r[reg]==machine.state().r[reg];
        if (!match && equal) std::cout<<"First semantic mismatch: "<<std::hex<<raw<<" flags "<<flags<<" pattern "<<pattern<<std::dec<<'\n';
        equal=equal&&match; ++comparisons;
    };
    for (uint32_t pattern : {0u,1u,0x7fffffffu,0x80000000u,0xffffffffu}) {
        for (uint32_t flags : {0u,FLAG_C,FLAG_N|FLAG_V,FLAG_Z|FLAG_C}) {
            for (unsigned code=0;code<16;++code) {
                for (uint32_t operand : {0x02000001u,0x02000480u,0x00000001u,0x00000021u,0x00000041u,0x00000061u,0x00000f81u}) {
                    const bool test=code>=8 && code<=11;
                    compare(0xe0000000u|operand|(code<<21)|(1u<<20)|(test?0u:2u<<12),arm,pattern,flags);
                    if (!test) compare(0xe0000000u|operand|(code<<21)|(2u<<12),arm,pattern,flags);
                }
            }
            for (uint32_t raw : {0x2007u,0x2801u,0x3001u,0x3801u,0x1842u,0x1c42u,0x1a42u,0x0042u,0x080au,0x100au,
                                 0x400au,0x404au,0x414au,0x418au,0x420au,0x424au,0x428au,0x42cau,0x430au,0x438au,0x43cau})
                compare(raw,thumb,pattern,flags);
        }
    }
    for (unsigned condition=0;condition<15;++condition)
        for (unsigned flags=0;flags<16;++flags)
            compare((condition<<28)|0x02802001u,arm,0x7fffffffu,flags<<28);
    check("Integer IR semantics agree with scalar ARM and Thumb execution",equal && comparisons>4000);
#ifdef OCEANBLAST_TEST_NATIVE
    check("Every semantic fixture executes generated x86-64 code",
        native.stats().generatedSteps==comparisons && native.stats().fallbackSteps==0 && native.stats().compilationFailures==0);
#endif
    std::cout<<comparisons<<" semantic comparisons; "<<failures<<" failure(s)\n";
    return failures?1:0;
}
