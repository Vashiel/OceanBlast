#include "x64_execution_engine.h"
#include "translation_frontend.h"
#include <asmjit/x86.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <array>
#include <vector>
#include <utility>
#include <new>

namespace oceanblast {
using namespace translation;
namespace {
asmjit::JitAllocator::CreateParams allocationParameters() {
    asmjit::JitAllocator::CreateParams parameters{};
    parameters.options=asmjit::JitAllocatorOptions::kUseDualMapping;
    return parameters;
}
class EmitErrors final : public asmjit::ErrorHandler {
public:
    asmjit::Error error=asmjit::Error::kOk;
    void handle_error(asmjit::Error value,const char*,asmjit::BaseEmitter*) override { error=value; }
};
bool executableReadOnly(const void* address) {
    MEMORY_BASIC_INFORMATION information{};
    return VirtualQuery(address,&information,sizeof(information)) &&
        information.State==MEM_COMMIT && (information.Protect&0xffu)==PAGE_EXECUTE_READ;
}
}
struct X64ExecutionEngine::Impl {
    using Function=void(*)(ARM920TState*);
    ARM920TState& architecturalState;
    ARM920T& cpu;
    Bus& bus;
    InterpreterEngine reference;
    asmjit::JitAllocator::CreateParams allocation=allocationParameters();
    asmjit::JitRuntime runtime{&allocation};
    NativeExecutionStats counters;
    struct CacheEntry {
        std::array<uint32_t,6> context;
        std::vector<std::pair<uint32_t,uint32_t>> source;
        Function function;
        size_t bytes;
        uint64_t used;
    };
    std::vector<CacheEntry> cache;
    uint64_t useCounter=0;
    size_t cacheBytes=0;
    static constexpr size_t maximumBlocks=64,maximumBytes=512*1024;
    Impl(ARM920TState& state,ARM920T& cpu,Bus& bus) : architecturalState(state),cpu(cpu),bus(bus),reference(cpu,bus) {
        if (&state!=&cpu.getState()) throw std::invalid_argument("Native and reference CPU state must match");
        cache.reserve(maximumBlocks);
    }
    ~Impl() { clearCache(); }
    void clearCache() {
        for (const auto& entry : cache) runtime.release(entry.function);
        cache.clear(); cacheBytes=0;
    }

    Function compile(const Block& block,size_t count,size_t& bytes) {
        using namespace asmjit;
        static_assert(std::is_standard_layout<ARM920TState>::value,"JIT register offsets require standard layout");
        EmitErrors errors;
        CodeHolder code;
        if (code.init(runtime.environment())!=Error::kOk) return nullptr;
        code.set_error_handler(&errors);
        x86::Compiler compiler(&code);
        auto pointer=compiler.new_gp_ptr("state");
        auto* function=compiler.add_func(FuncSignature::build<void,ARM920TState*>());
        if (!function) return nullptr;
        function->set_arg(0,pointer);
        auto load=[&](Value value) {
            auto target=compiler.new_gp32();
            if (value.kind==ValueKind::Register)
                compiler.mov(target,x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+value.value*4)));
            else compiler.mov(target,value.value);
            return target;
        };
        auto bit=[&](x86::Gp source,unsigned position) {
            auto result=compiler.new_gp32(); compiler.mov(result,source);
            compiler.shr(result,position); compiler.and_(result,1); return result;
        };
        auto booleanNot=[&](x86::Gp value) { compiler.xor_(value,1); return value; };
        auto capture=[&](x86::Gp value) { compiler.movzx(value,value.r8()); };
        for (size_t index=0;index<count;++index) {
            const auto& instruction=block.instructions[index];
            const auto& op=instruction.operation;
            auto flags=compiler.new_gp32();
            compiler.mov(flags,x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,cpsr))));
            Label skipped=compiler.new_label();
            if (instruction.condition!=Condition::Always) {
                auto n=bit(flags,31),z=bit(flags,30),c=bit(flags,29),v=bit(flags,28);
                auto predicate=compiler.new_gp32();
                switch(instruction.condition) {
                    case Condition::Eq:compiler.mov(predicate,z);break;
                    case Condition::Ne:compiler.mov(predicate,booleanNot(z));break;
                    case Condition::Cs:compiler.mov(predicate,c);break;
                    case Condition::Cc:compiler.mov(predicate,booleanNot(c));break;
                    case Condition::Mi:compiler.mov(predicate,n);break;
                    case Condition::Pl:compiler.mov(predicate,booleanNot(n));break;
                    case Condition::Vs:compiler.mov(predicate,v);break;
                    case Condition::Vc:compiler.mov(predicate,booleanNot(v));break;
                    case Condition::Hi:compiler.mov(predicate,c);compiler.and_(predicate,booleanNot(z));break;
                    case Condition::Ls:compiler.mov(predicate,booleanNot(c));compiler.or_(predicate,z);break;
                    case Condition::Ge:compiler.mov(predicate,n);compiler.xor_(predicate,v);booleanNot(predicate);break;
                    case Condition::Lt:compiler.mov(predicate,n);compiler.xor_(predicate,v);break;
                    case Condition::Gt:compiler.mov(predicate,n);compiler.xor_(predicate,v);booleanNot(predicate);compiler.and_(predicate,booleanNot(z));break;
                    case Condition::Le:compiler.mov(predicate,n);compiler.xor_(predicate,v);compiler.or_(predicate,z);break;
                    case Condition::Always:break;
                }
                compiler.test(predicate,predicate); compiler.je(skipped);
            }
            auto left=load(op.left),right=load(op.right);
            auto carry=bit(flags,29);
            if (op.shifterCarry==CarrySource::Zero) compiler.mov(carry,0);
            else if (op.shifterCarry==CarrySource::One) compiler.mov(carry,1);
            else if (op.shifterCarry==CarrySource::Shifter) {
                const unsigned amount=op.shiftAmount;
                if ((op.shift==Shift::Lsr || op.shift==Shift::Asr) && amount==32) {
                    compiler.mov(carry,right); compiler.shr(carry,31);
                    if (op.shift==Shift::Lsr) compiler.mov(right,0); else compiler.sar(right,31);
                } else {
                    switch(op.shift) {
                        case Shift::Lsl:compiler.shl(right,amount);break;
                        case Shift::Lsr:compiler.shr(right,amount);break;
                        case Shift::Asr:compiler.sar(right,amount);break;
                        case Shift::Ror:compiler.ror(right,amount);break;
                        case Shift::Rrx:compiler.bt(flags,29);compiler.rcr(right,1);break;
                    }
                    compiler.setb(carry.r8()); capture(carry);
                }
            }
            const bool subtraction=op.opcode==IntegerOpcode::Subtract || op.opcode==IntegerOpcode::ReverseSubtract ||
                op.opcode==IntegerOpcode::SubtractCarry || op.opcode==IntegerOpcode::ReverseSubtractCarry;
            if (op.opcode==IntegerOpcode::ReverseSubtract || op.opcode==IntegerOpcode::ReverseSubtractCarry)
                std::swap(left,right);
            switch(op.opcode) {
                case IntegerOpcode::And:compiler.and_(left,right);break;
                case IntegerOpcode::Xor:compiler.xor_(left,right);break;
                case IntegerOpcode::Subtract:case IntegerOpcode::ReverseSubtract:compiler.sub(left,right);break;
                case IntegerOpcode::Add:compiler.add(left,right);break;
                case IntegerOpcode::AddCarry:compiler.bt(flags,29);compiler.adc(left,right);break;
                case IntegerOpcode::SubtractCarry:case IntegerOpcode::ReverseSubtractCarry:
                    compiler.bt(flags,29);compiler.cmc();compiler.sbb(left,right);break;
                case IntegerOpcode::Or:compiler.or_(left,right);break;
                case IntegerOpcode::Move:compiler.mov(left,right);break;
                case IntegerOpcode::BitClear:compiler.not_(right);compiler.and_(left,right);break;
                case IntegerOpcode::Not:compiler.not_(right);compiler.mov(left,right);break;
            }
            if (op.flags!=FlagWrite::Preserve) {
                auto negative=compiler.new_gp32(),zero=compiler.new_gp32(),overflow=compiler.new_gp32();
                if (op.flags==FlagWrite::Logical) compiler.test(left,left);
                compiler.sets(negative.r8()); compiler.sete(zero.r8());
                if (op.flags==FlagWrite::Arithmetic) {
                    compiler.setb(carry.r8()); compiler.seto(overflow.r8());
                }
                capture(negative); capture(zero); capture(carry);
                if (op.flags==FlagWrite::Arithmetic) {
                    capture(overflow);
                    if (subtraction) booleanNot(carry);
                    compiler.and_(flags,0x0fffffffu);
                    compiler.shl(overflow,28); compiler.or_(flags,overflow);
                } else compiler.and_(flags,0x1fffffffu);
                compiler.shl(negative,31); compiler.shl(zero,30); compiler.shl(carry,29);
                compiler.or_(flags,negative); compiler.or_(flags,zero); compiler.or_(flags,carry);
                compiler.mov(x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,cpsr))),flags);
            }
            if (op.destination)
                compiler.mov(x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+*op.destination*4)),left);
            compiler.bind(skipped);
        }
        const auto& last=block.instructions[count-1];
        compiler.mov(x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+15*4)),last.pc+last.bytes);
        compiler.ret(); compiler.end_func();
        if (compiler.finalize()!=Error::kOk || errors.error!=Error::kOk) return nullptr;
        bytes=code.code_size();
        if (!bytes || bytes>64*1024) return nullptr;
        Function result=nullptr;
        if (runtime.add(&result,&code)!=Error::kOk) return nullptr;
        if (!executableReadOnly(reinterpret_cast<const void*>(result))) { runtime.release(result); return nullptr; }
        ++counters.compilations;
        return result;
    }
    Function findOrCompile(const Block& block,const std::vector<std::pair<uint32_t,uint32_t>>& source) {
        const auto& state=cpu.getState();
        const std::array<uint32_t,6> context={block.entryPc,state.cpsr&0xffu,state.cp15_control,
            state.cp15_ttb,state.cp15_dacr,uint32_t(block.instructions.size())};
        // Fetch every source word with current permissions before cache lookup.
        // Exact physical-address/encoding checks also cover writes through aliases.
        for (auto& entry : cache) {
            if (entry.context==context && entry.source==source) {
                entry.used=++useCounter; ++counters.cacheHits; return entry.function;
            }
        }
        size_t bytes=0;
        auto function=compile(block,block.instructions.size(),bytes);
        if (!function) return nullptr;
        while (cache.size()>=maximumBlocks || cacheBytes+bytes>maximumBytes) {
            auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& left,const auto& right){return left.used<right.used;});
            cacheBytes-=oldest->bytes; runtime.release(oldest->function); cache.erase(oldest);
            ++counters.cacheEvictions;
        }
        try { cache.push_back({context,source,function,bytes,++useCounter}); }
        catch (...) { runtime.release(function); throw; }
        cacheBytes+=bytes;
        return function;
    }
    Function reusable(size_t limit,size_t& count) {
        const auto& state=cpu.getState();
        const uint32_t pc=state.r[15],width=(state.cpsr&FLAG_T)?2:4;
        Bus::MmuFault fault=Bus::MmuFault::NONE;
        if (cache.empty()) return nullptr;
        const uint32_t physical=bus.translate(pc,&fault);
        if (fault!=Bus::MmuFault::NONE) return nullptr;
        CacheEntry* best=nullptr;
        for (auto& entry : cache) {
            const auto& key=entry.context;
            if (key[0]!=pc || key[1]!=(state.cpsr&0xffu) || key[2]!=state.cp15_control ||
                key[3]!=state.cp15_ttb || key[4]!=state.cp15_dacr || key[5]>limit ||
                (best && key[5]<=best->context[5])) continue;
            const uint32_t bytes=key[5]*width;
            // Like the reference fetch cache, a native block stays in one 1 KB
            // permission span. One checked translation establishes its mapping;
            // every source word is still checked for self-modifying/alias writes.
            if (((pc^(pc+bytes-1))&~1023u) || (physical&1023u)!=(pc&1023u)) continue;
            const bool ram=(physical<ADDR_STEPPINGSTONE_SIZE && bytes<=ADDR_STEPPINGSTONE_SIZE-physical) ||
                (physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE && bytes<=ADDR_SDRAM_SIZE-(physical-ADDR_SDRAM_BASE));
            if (!ram) continue;
            bool valid=true;
            for (size_t index=0;index<entry.source.size();++index) {
                const uint32_t address=physical+uint32_t(index)*width;
                if (entry.source[index].first!=address || entry.source[index].second!=
                    (width==4?bus.read32Phys(address):uint32_t(bus.read16Phys(address)))) { valid=false; break; }
            }
            if (valid) best=&entry;
        }
        if (!best) return nullptr;
        best->used=++useCounter; ++counters.cacheHits;
        count=best->source.size();
        return best->function;
    }
    ExecutionExit fallback(ExecutionTimeline& timeline,size_t budget,uint64_t deadline) {
        auto result=reference.runToBoundary(timeline,std::min<size_t>(budget,1),deadline);
        counters.fallbackSteps+=result.steps;
        return result;
    }
};
X64ExecutionEngine::X64ExecutionEngine(ARM920TState& state,ARM920T& cpu,Bus& bus) : impl(std::make_unique<Impl>(state,cpu,bus)) {}
X64ExecutionEngine::~X64ExecutionEngine()=default;
const ARM920TState& X64ExecutionEngine::state() const { return impl->cpu.getState(); }
const Bus& X64ExecutionEngine::memory() const { return impl->bus; }
bool X64ExecutionEngine::canHandoff() const { return impl->cpu.canHandoff(); }
void X64ExecutionEngine::enter(bool clocks) { impl->cpu.enterExecution(clocks); }
const NativeExecutionStats& X64ExecutionEngine::stats() const { return impl->counters; }
void X64ExecutionEngine::resetCodeCache() { impl->clearCache(); ++impl->counters.cacheResets; }
ExecutionExit X64ExecutionEngine::runToBoundary(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) {
    auto& engine=*impl;
    const auto& state=engine.cpu.getState();
    // Complex entry conditions and the experimental clock model retain the
    // scalar contract. The legacy title hook is excluded from native fixtures.
    if (!budget || state.halted || state.waitingForInterrupt || timeline.registerClocks ||
        timeline.elapsed.ticks>=stopTicks || engine.bus.wadeTitleBlitEnabled() ||
        (engine.bus.hasPendingIrq() && !(state.cpsr&FLAG_I)) || !engine.cpu.canHandoff())
        return engine.fallback(timeline,budget,stopTicks);
    const uint64_t remaining=std::min(engine.bus.ticksUntilEvent(),stopTicks-timeline.elapsed.ticks);
    size_t count=std::min<size_t>(budget,32);
    if (engine.bus.needsLevelInterruptRefresh()) count=1;
    if (remaining<=count) count=std::min<uint64_t>(count,legacyStepsUntilTickLimit(remaining,timeline.ratio,timeline.phase));
    size_t nativeCount=0;
    auto function=engine.reusable(count,nativeCount);
    if (!function) {
        try {
            std::vector<std::pair<uint32_t,uint32_t>> source;
            source.reserve(count);
            auto fetch=[&](uint32_t address,InstructionSet set) {
                Bus::MmuFault fault=Bus::MmuFault::NONE;
                const uint32_t physical=engine.bus.translate(address,&fault);
                const bool ram=physical<ADDR_STEPPINGSTONE_SIZE || physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE;
                if (fault!=Bus::MmuFault::NONE || !ram) return FetchResult{false,0};
                const uint32_t encoding=set==InstructionSet::Arm?engine.bus.read32Phys(physical):uint32_t(engine.bus.read16Phys(physical));
                source.emplace_back(physical,encoding);
                return FetchResult{true,encoding};
            };
            auto block=decodeBlock(state.r[15],(state.cpsr&FLAG_T)?InstructionSet::Thumb:InstructionSet::Arm,fetch,{uint32_t(count),128});
            nativeCount=block.instructions.size();
            if (!nativeCount) return engine.fallback(timeline,budget,stopTicks);
            source.resize(nativeCount);
            function=engine.findOrCompile(block,source);
            if (!function) { ++engine.counters.compilationFailures; return engine.fallback(timeline,budget,stopTicks); }
        } catch (const std::bad_alloc&) {
            ++engine.counters.compilationFailures;
            return engine.fallback(timeline,budget,stopTicks);
        }
    }
    // No guest MMIO/memory operation is lowered in this slice. Stop no later
    // than the next device event, then advance the same legacy tick count.
    function(&engine.architecturalState);
    const uint64_t ticks=(timeline.phase+nativeCount-1)/timeline.ratio+(timeline.phase==0?1:0);
    timeline.phase=(timeline.phase+nativeCount)%timeline.ratio;
    engine.bus.tick(ticks);
    timeline.elapsed.ticks+=ticks;
    engine.counters.generatedSteps+=nativeCount;
    const auto reason=ticks>=remaining?ExecutionExitReason::Deadline:
        (nativeCount==budget?ExecutionExitReason::Budget:ExecutionExitReason::BlockEnd);
    return {reason,nativeCount,ticks,std::nullopt};
}
} // namespace oceanblast
