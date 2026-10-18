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
#include <list>
#include <unordered_map>

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
struct NativeRun {
    ExecutionTimeline* timeline;
    ARM920TState* state;
    uint32_t physical;
    uint32_t limit;
    uint32_t retired=0,tickedSteps=0,memorySteps=0;
};
void advanceIntegerPrefix(Bus* bus,ExecutionTimeline* timeline,uint32_t count) {
    const uint64_t ticks=(timeline->phase+count-1)/timeline->ratio+(timeline->phase==0?1:0);
    timeline->phase=(timeline->phase+count)%timeline->ratio;
    bus->tick(ticks); timeline->elapsed.ticks+=ticks;
}
// Entry is bounded by the first device event. Ordinary RAM cannot arm a new
// event or change execution context, so its ticks can be committed together.
// Before a device access or fault, commit all preceding instructions first.
void materializeBeforeMemory(Bus* bus,NativeRun* run) {
    const uint32_t completed=run->retired-1;
    if(completed>run->tickedSteps) {
        const auto pc=run->state->r[15]; run->state->r[15]=pc-4;
        advanceIntegerPrefix(bus,run->timeline,completed-run->tickedSteps);
        run->state->r[15]=pc; run->tickedSteps=completed;
    }
}
bool ordinaryRam(uint32_t physical) {
    return physical<ADDR_STEPPINGSTONE_SIZE || physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE;
}
template<bool Byte> uint64_t checkedLoad(Bus* bus,NativeRun* run,uint32_t address) {
    ++run->memorySteps;
    uint32_t physical=0;
    if(!bus->translateDataAddress(Byte?address:address&~3u,false,physical)) {
        materializeBeforeMemory(bus,run); return uint64_t(1)<<32;
    }
    const bool ram=ordinaryRam(physical);
    if(!ram) materializeBeforeMemory(bus,run);
    uint32_t value=Byte?bus->read8Phys(physical):bus->read32Phys(physical);
    if (!Byte && (address&3u)) { const unsigned shift=(address&3u)*8; value=(value>>shift)|(value<<(32-shift)); }
    return (uint64_t(ram?0:2)<<32)|value;
}
template<bool Byte> uint32_t checkedStore(Bus* bus,NativeRun* run,uint32_t address,uint32_t value) {
    ++run->memorySteps;
    uint32_t physical=0;
    if(!bus->translateDataAddress(Byte?address:address&~3u,true,physical)) {
        materializeBeforeMemory(bus,run); return 1;
    }
    const bool ram=ordinaryRam(physical);
    if(!ram) materializeBeforeMemory(bus,run);
    if (Byte) bus->write8Phys(physical,uint8_t(value)); else bus->write32Phys(physical,value);
    if(!ram) return 2;
    // A store to the currently executing physical code page must return before
    // any following translated instruction, including writes through aliases.
    return (physical&~1023u)==(run->physical&~1023u)?4:0;
}
void checkedAbort(ARM920T* cpu) { cpu->completeCheckedDataAbort(); }
bool executableReadOnly(const void* address) {
    MEMORY_BASIC_INFORMATION information{};
    return VirtualQuery(address,&information,sizeof(information)) &&
        information.State==MEM_COMMIT && (information.Protect&0xffu)==PAGE_EXECUTE_READ;
}
}
struct X64ExecutionEngine::Impl {
    using Function=uint32_t(*)(ARM920TState*,NativeRun*);
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
        uint64_t mapping=0,epoch=0;
        uint32_t version=0;
        bool nativeEligible=false;
        size_t visits=0;
    };
    using Cache=std::list<CacheEntry>;
    Cache cache;
    struct HotEntry { CacheEntry* entry=nullptr; Cache::iterator iterator; size_t limit=0; };
    static constexpr size_t hotCount=1024;
    std::array<HotEntry,hotCount> hot{};
    static size_t hotIndex(uint32_t pc) { return ((pc>>1)^(pc>>12))&(hotCount-1); }
    std::unordered_multimap<uint32_t,Cache::iterator> cacheByPc;
    NativeCacheLimits limits;
    uint64_t useCounter=0;
    size_t cacheBytes=0;
    bool eventWindowDirty=true;
    Impl(ARM920TState& state,ARM920T& cpu,Bus& bus,NativeCacheLimits limits) : architecturalState(state),cpu(cpu),bus(bus),reference(cpu,bus),limits(limits) {
        if (&state!=&cpu.getState()) throw std::invalid_argument("Native and reference CPU state must match");
        if (!limits.blocks || !limits.bytes || !limits.compileThreshold) throw std::invalid_argument("Native cache limits must be positive");
        cacheByPc.reserve(std::min<size_t>(limits.blocks,4096));
    }
    ~Impl() { clearCache(); }
    void refreshDependency(CacheEntry& entry) {
        entry.mapping=bus.getMappingGeneration(); entry.epoch=bus.getCodeEpoch();
        entry.version=bus.markCodePage(entry.source.front().first);
    }
    void clearCache() {
        hot.fill({});
        for (const auto& entry : cache) if(entry.function) runtime.release(entry.function);
        cacheByPc.clear(); cache.clear(); cacheBytes=0;
    }

    Function publish(asmjit::CodeHolder& code,size_t& bytes) {
        bytes=code.code_size();
        if (!bytes || bytes>64*1024) return nullptr;
        Function result=nullptr;
        if (runtime.add(&result,&code)!=asmjit::Error::kOk) return nullptr;
        if (!executableReadOnly(reinterpret_cast<const void*>(result))) { runtime.release(result); return nullptr; }
        ++counters.compilations;
        return result;
    }
    asmjit::x86::Gp emitMemory(asmjit::x86::Compiler& cc,asmjit::x86::Gp pointer,asmjit::x86::Gp run,const Instruction& instruction) {
        using namespace asmjit;
        const auto& memory=*instruction.memory;
        auto reg=[&](unsigned index) { return x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+index*4)); };
        cc.mov(reg(15),instruction.pc+4);
        auto base=cc.new_gp32(),updated=cc.new_gp32(),address=cc.new_gp32();
        cc.mov(base,reg(memory.base)); cc.mov(updated,base);
        if (memory.up) cc.add(updated,memory.offset); else cc.sub(updated,memory.offset);
        cc.mov(address,memory.pre?updated:base);
        auto busPointer=cc.new_gp_ptr();
        cc.mov(busPointer,uintptr_t(&bus));
        auto error=cc.new_gp32(),status=cc.new_gp32();
        InvokeNode* invoke=nullptr;
        x86::Gp data;
        if (memory.load) {
            auto result=cc.new_gp64();
            cc.invoke(Out(invoke),imm(memory.byte?(void*)checkedLoad<true>:(void*)checkedLoad<false>),FuncSignature::build<uint64_t,Bus*,NativeRun*,uint32_t>());
            invoke->set_arg(0,busPointer); invoke->set_arg(1,run); invoke->set_arg(2,address); invoke->set_ret(0,result);
            auto high=cc.new_gp64(); cc.mov(high,result); cc.shr(high,32); cc.mov(error,high.r32());
            data=result.r32();
        } else {
            auto value=cc.new_gp32(); cc.mov(value,reg(memory.destination));
            cc.invoke(Out(invoke),imm(memory.byte?(void*)checkedStore<true>:(void*)checkedStore<false>),FuncSignature::build<uint32_t,Bus*,NativeRun*,uint32_t,uint32_t>());
            invoke->set_arg(0,busPointer); invoke->set_arg(1,run); invoke->set_arg(2,address); invoke->set_arg(3,value); invoke->set_ret(0,error);
        }
        Label abort=cc.new_label(),done=cc.new_label();
        cc.test(error,1); cc.jnz(abort);
        if (memory.load) cc.mov(reg(memory.destination),data);
        if (memory.writeback) cc.mov(reg(memory.base),updated);
        cc.mov(status,error); cc.jmp(done);
        cc.bind(abort);
        auto cpuPointer=cc.new_gp_ptr(); cc.mov(cpuPointer,uintptr_t(&cpu));
        cc.invoke(Out(invoke),imm((void*)checkedAbort),FuncSignature::build<void,ARM920T*>());
        invoke->set_arg(0,cpuPointer);
        cc.mov(status,1);
        cc.bind(done); return status;
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
        auto run=compiler.new_gp_ptr("run");
        auto* function=compiler.add_func(FuncSignature::build<uint32_t,ARM920TState*,NativeRun*>());
        if (!function) return nullptr;
        function->set_arg(0,pointer);
        function->set_arg(1,run);
        auto load=[&](Value value) {
            auto target=compiler.new_gp32();
            if (value.kind==ValueKind::Register)
                compiler.mov(target,x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+value.value*4)));
            else compiler.mov(target,value.value);
            return target;
        };
        auto booleanNot=[&](x86::Gp value) { compiler.xor_(value,1); return value; };
        auto capture=[&](x86::Gp value) { compiler.movzx(value,value.r8()); };
        Label blockExit=compiler.new_label(),blockStart=compiler.new_label();
        const auto& terminal=block.instructions[count-1];
        const bool repeatable=terminal.branch && !terminal.branch->link && terminal.branch->target==block.entryPc;
        auto loopBase=compiler.new_gp32();
        if(repeatable) compiler.mov(loopBase,0);
        compiler.bind(blockStart);
        for (size_t index=0;index<count;++index) {
            const auto& instruction=block.instructions[index];
            const auto& op=instruction.operation;
            auto flags=compiler.new_gp32();
            const bool logicalFlags=op.flags==FlagWrite::Logical;
            const bool usesCarry=op.opcode==IntegerOpcode::AddCarry || op.opcode==IntegerOpcode::SubtractCarry ||
                op.opcode==IntegerOpcode::ReverseSubtractCarry || op.shift==Shift::Rrx;
            if(instruction.condition!=Condition::Always || op.flags!=FlagWrite::Preserve || usesCarry)
                compiler.mov(flags,x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,cpsr))));
            Label skipped=compiler.new_label();
            if (instruction.condition!=Condition::Always) {
                auto skipBit=[&](uint32_t mask,bool whenSet) {
                    compiler.test(flags,mask);
                    if(whenSet) compiler.jnz(skipped); else compiler.jz(skipped);
                };
                auto testNvDifference=[&]() {
                    auto difference=compiler.new_gp32(); compiler.mov(difference,flags);
                    compiler.shl(difference,3); compiler.xor_(difference,flags); compiler.test(difference,FLAG_N);
                };
                switch(instruction.condition) {
                    case Condition::Eq:skipBit(FLAG_Z,false);break;
                    case Condition::Ne:skipBit(FLAG_Z,true);break;
                    case Condition::Cs:skipBit(FLAG_C,false);break;
                    case Condition::Cc:skipBit(FLAG_C,true);break;
                    case Condition::Mi:skipBit(FLAG_N,false);break;
                    case Condition::Pl:skipBit(FLAG_N,true);break;
                    case Condition::Vs:skipBit(FLAG_V,false);break;
                    case Condition::Vc:skipBit(FLAG_V,true);break;
                    case Condition::Hi:case Condition::Ls: {
                        auto pair=compiler.new_gp32(); compiler.mov(pair,flags); compiler.and_(pair,FLAG_C|FLAG_Z);
                        compiler.cmp(pair,FLAG_C);
                        if(instruction.condition==Condition::Hi) compiler.jne(skipped); else compiler.je(skipped);
                        break;
                    }
                    case Condition::Ge:testNvDifference();compiler.jnz(skipped);break;
                    case Condition::Lt:testNvDifference();compiler.jz(skipped);break;
                    case Condition::Gt:skipBit(FLAG_Z,true);testNvDifference();compiler.jnz(skipped);break;
                    case Condition::Le: {
                        const auto passed=compiler.new_label(); compiler.test(flags,FLAG_Z); compiler.jnz(passed);
                        testNvDifference(); compiler.jz(skipped); compiler.bind(passed); break;
                    }
                    case Condition::Always:break;
                }
            }
            if(instruction.memory) {
                if(repeatable) {
                    auto completed=compiler.new_gp32(); compiler.mov(completed,loopBase); compiler.add(completed,uint32_t(index+1));
                    compiler.mov(x86::dword_ptr(run,int32_t(offsetof(NativeRun,retired))),completed);
                } else compiler.mov(x86::dword_ptr(run,int32_t(offsetof(NativeRun,retired))),uint32_t(index+1));
                auto status=emitMemory(compiler,pointer,run,instruction);
                auto next=compiler.new_label(); compiler.test(status,status); compiler.je(next);
                compiler.ret(status); compiler.bind(next);
                compiler.bind(skipped); continue;
            }
            if(instruction.branch) {
                if(instruction.branch->link)
                    compiler.mov(x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+14*4)),instruction.pc+instruction.bytes);
                compiler.mov(x86::dword_ptr(pointer,int32_t(offsetof(ARM920TState,r)+15*4)),instruction.branch->target);
                if(repeatable) {
                    compiler.add(loopBase,uint32_t(count));
                    auto next=compiler.new_gp32(); compiler.mov(next,loopBase); compiler.add(next,uint32_t(count));
                    compiler.cmp(next,x86::dword_ptr(run,int32_t(offsetof(NativeRun,limit)))); compiler.jbe(blockStart);
                }
                compiler.jmp(blockExit); compiler.bind(skipped); continue;
            }
            auto right=load(op.right);
            const bool unary=op.opcode==IntegerOpcode::Move || op.opcode==IntegerOpcode::Not;
            auto left=unary?right:load(op.left);
            auto carry=compiler.new_gp32();
            if (op.shifterCarry==CarrySource::Zero) { if(logicalFlags) compiler.mov(carry,0); }
            else if (op.shifterCarry==CarrySource::One) { if(logicalFlags) compiler.mov(carry,1); }
            else if (op.shifterCarry==CarrySource::Shifter) {
                const unsigned amount=op.shiftAmount;
                if ((op.shift==Shift::Lsr || op.shift==Shift::Asr) && amount==32) {
                    if(logicalFlags) { compiler.mov(carry,right); compiler.shr(carry,31); }
                    if (op.shift==Shift::Lsr) compiler.mov(right,0); else compiler.sar(right,31);
                } else {
                    switch(op.shift) {
                        case Shift::Lsl:compiler.shl(right,amount);break;
                        case Shift::Lsr:compiler.shr(right,amount);break;
                        case Shift::Asr:compiler.sar(right,amount);break;
                        case Shift::Ror:compiler.ror(right,amount);break;
                        case Shift::Rrx:compiler.bt(flags,29);compiler.rcr(right,1);break;
                    }
                    if(logicalFlags) { compiler.setb(carry.r8()); capture(carry); }
                }
            } else if(logicalFlags) { compiler.mov(carry,flags); compiler.shr(carry,29); compiler.and_(carry,1); }
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
                case IntegerOpcode::Move:break;
                case IntegerOpcode::BitClear:compiler.not_(right);compiler.and_(left,right);break;
                case IntegerOpcode::Not:compiler.not_(left);break;
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
        if(repeatable) compiler.add(loopBase,uint32_t(count));
        compiler.bind(blockExit);
        if(repeatable) compiler.mov(x86::dword_ptr(run,int32_t(offsetof(NativeRun,retired))),loopBase);
        else compiler.mov(x86::dword_ptr(run,int32_t(offsetof(NativeRun,retired))),uint32_t(count));
        auto status=compiler.new_gp32(); compiler.mov(status,0); compiler.ret(status); compiler.end_func();
        if (compiler.finalize()!=Error::kOk || errors.error!=Error::kOk) return nullptr;
        return publish(code,bytes);
    }
    void evictOldest() {
        auto oldest=cache.begin();
        auto& slot=hot[hotIndex(oldest->context[0])];
        if(slot.entry==&*oldest) slot={};
        const auto range=cacheByPc.equal_range(oldest->context[0]);
        for(auto candidate=range.first;candidate!=range.second;++candidate)
            if(candidate->second==oldest) { cacheByPc.erase(candidate); break; }
        cacheBytes-=oldest->bytes;
        if(oldest->function) runtime.release(oldest->function);
        cache.erase(oldest); ++counters.cacheEvictions;
    }
    void promote(CacheEntry& entry) {
        const auto set=(entry.context[1]&FLAG_T)?InstructionSet::Thumb:InstructionSet::Arm;
        const uint32_t width=set==InstructionSet::Arm?4:2;
        auto fetch=[&](uint32_t address,InstructionSet) {
            const size_t index=(address-entry.context[0])/width;
            return index<entry.source.size()?FetchResult{true,entry.source[index].second}:FetchResult{false,0};
        };
        auto block=decodeBlock(entry.context[0],set,fetch,{entry.context[5],128});
        size_t bytes=0;
        auto function=compile(block,block.instructions.size(),bytes);
        if(!function || bytes>limits.bytes) {
            if(function) runtime.release(function);
            ++counters.compilationFailures; entry.visits=0; return;
        }
        // The selected entry is the most recently used node and has no code
        // yet. The per-function check guarantees it survives byte eviction.
        while(cacheBytes+bytes>limits.bytes) evictOldest();
        entry.function=function; entry.bytes=bytes; cacheBytes+=bytes;
        counters.peakCacheBytes=std::max<uint64_t>(counters.peakCacheBytes,cacheBytes);
        ++counters.promotions;
    }
    CacheEntry* findOrCompile(const Block& block,const std::vector<std::pair<uint32_t,uint32_t>>& source) {
        const auto& state=cpu.getState();
        // I/F masks affect entry/IRQ delivery, not emitted instruction semantics.
        const std::array<uint32_t,6> context={block.entryPc,(state.cpsr&0x3fu)|(bus.isUserMode()?0x100u:0u),state.cp15_control,
            state.cp15_ttb,state.cp15_dacr,uint32_t(source.size())};
        // Fetch every source word with current permissions before cache lookup.
        // Exact physical-address/encoding checks also cover writes through aliases.
        const auto candidates=cacheByPc.equal_range(context[0]);
        for (auto candidate=candidates.first;candidate!=candidates.second;++candidate) {
            auto iterator=candidate->second;
            auto& entry=*iterator;
            if (entry.context==context && entry.source==source) {
                refreshDependency(entry);
                cache.splice(cache.end(),cache,iterator);
                entry.used=++useCounter;
                if(entry.function) ++counters.cacheHits;
                else if(entry.nativeEligible) ++counters.coldCacheHits; else ++counters.fallbackCacheHits;
                return &entry;
            }
        }
        size_t bytes=0;
        const bool scalar=block.instructions.empty();
        const bool deferred=!scalar && limits.compileThreshold>1;
        auto function=(scalar||deferred)?nullptr:compile(block,block.instructions.size(),bytes);
        if (!function && !scalar && !deferred) return nullptr;
        if (bytes>limits.bytes) { runtime.release(function); return nullptr; }
        while (cache.size()>=limits.blocks || cacheBytes+bytes>limits.bytes) evictOldest();
        try {
            cache.push_back({context,source,function,bytes,++useCounter});
            cache.back().nativeEligible=!scalar;
            refreshDependency(cache.back());
            try { cacheByPc.emplace(context[0],std::prev(cache.end())); }
            catch (...) { cache.pop_back(); throw; }
        } catch (...) { if(function) runtime.release(function); throw; }
        cacheBytes+=bytes;
        counters.peakCacheEntries=std::max<uint64_t>(counters.peakCacheEntries,cache.size());
        counters.peakCacheBytes=std::max<uint64_t>(counters.peakCacheBytes,cacheBytes);
        hot[hotIndex(context[0])]={};
        if(scalar) ++counters.fallbackEntries;
        return &cache.back();
    }
    CacheEntry* reusable(size_t limit) {
        const auto& state=cpu.getState();
        const uint32_t pc=state.r[15],width=(state.cpsr&FLAG_T)?2:4;
        Bus::MmuFault fault=Bus::MmuFault::NONE;
        if (cache.empty()) return nullptr;
        auto eligible=[&](const CacheEntry& entry) {
            const auto& key=entry.context;
            return key[0]==pc && key[1]==((state.cpsr&0x3fu)|(bus.isUserMode()?0x100u:0u)) && key[2]==state.cp15_control &&
                key[3]==state.cp15_ttb && key[4]==state.cp15_dacr && key[5]<=limit;
        };
        auto unchanged=[&](const CacheEntry& entry) {
            return entry.mapping==bus.getMappingGeneration() && entry.epoch==bus.getCodeEpoch() &&
                entry.version==bus.getCodePageVersion(entry.source.front().first);
        };
        auto touch=[&](Cache::iterator iterator,bool fast) {
            cache.splice(cache.end(),cache,iterator); iterator->used=++useCounter;
            if(iterator->function) { ++counters.cacheHits; if(fast) ++counters.fastCacheHits; }
            else if(iterator->nativeEligible) ++counters.coldCacheHits;
            else ++counters.fallbackCacheHits;
            return &*iterator;
        };
        auto& slot=hot[hotIndex(pc)];
        // Match the lookup budget as well: a previously selected short entry
        // must not hide a longer eligible translation on a larger lookup.
        if(slot.entry && slot.limit==limit && eligible(*slot.entry) && unchanged(*slot.entry)) {
            ++counters.directCacheHits; return touch(slot.iterator,true);
        }
        uint32_t physical=0;
        bool translated=false,bestFast=false;
        auto best=cache.end();
        const auto range=cacheByPc.equal_range(pc);
        for (auto candidate=range.first;candidate!=range.second;++candidate) {
            auto iterator=candidate->second;
            auto& entry=*iterator;
            const auto& key=entry.context;
            if (!eligible(entry) ||
                (best!=cache.end() && key[5]<=best->context[5])) continue;
            if(unchanged(entry)) {
                best=iterator; bestFast=true; continue;
            }
            if(!translated) {
                physical=bus.translate(pc,&fault); translated=true;
                if(fault!=Bus::MmuFault::NONE) return nullptr;
            }
            const uint32_t bytes=key[5]*width;
            // Like the reference fetch cache, a native block stays in one 1 KB
            // permission span. One checked translation establishes its mapping;
            // every source word is still checked for self-modifying/alias writes.
            if (((pc^(pc+bytes-1))&~1023u) || (physical&1023u)!=(pc&1023u)) continue;
            const bool ram=(physical<ADDR_STEPPINGSTONE_SIZE && bytes<=ADDR_STEPPINGSTONE_SIZE-physical) ||
                (physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE && bytes<=ADDR_SDRAM_SIZE-(physical-ADDR_SDRAM_BASE));
            if (!ram) continue;
            // A permission-checked translation to the same physical source
            // does not require rereading bytes when RAM/DMA versions agree.
            if(entry.source.front().first==physical && entry.epoch==bus.getCodeEpoch() &&
                entry.version==bus.getCodePageVersion(physical)) {
                entry.mapping=bus.getMappingGeneration(); ++counters.mappingRevalidations;
                best=iterator; bestFast=false; continue;
            }
            ++counters.sourceValidations;
            bool valid=true;
            for (size_t index=0;index<entry.source.size();++index) {
                const uint32_t address=physical+uint32_t(index)*width;
                if (entry.source[index].first!=address || entry.source[index].second!=
                    (width==4?bus.read32Phys(address):uint32_t(bus.read16Phys(address)))) { valid=false; break; }
            }
            if (valid) { refreshDependency(entry); best=iterator; bestFast=false; }
        }
        if (best==cache.end()) return nullptr;
        slot={&*best,best,limit};
        return touch(best,bestFast);
    }
    ExecutionExit fallback(ExecutionTimeline& timeline,size_t budget,uint64_t deadline) {
        eventWindowDirty=true;
        auto result=reference.runToBoundary(timeline,std::min<size_t>(budget,1),deadline);
        counters.fallbackSteps+=result.steps;
        return result;
    }
};
X64ExecutionEngine::X64ExecutionEngine(ARM920TState& state,ARM920T& cpu,Bus& bus,NativeCacheLimits limits) : impl(std::make_unique<Impl>(state,cpu,bus,limits)) {}
X64ExecutionEngine::~X64ExecutionEngine()=default;
const ARM920TState& X64ExecutionEngine::state() const { return impl->cpu.getState(); }
const Bus& X64ExecutionEngine::memory() const { return impl->bus; }
bool X64ExecutionEngine::canHandoff() const { return impl->cpu.canHandoff(); }
void X64ExecutionEngine::enter(bool clocks) { impl->cpu.enterExecution(clocks); }
const NativeExecutionStats& X64ExecutionEngine::stats() const { return impl->counters; }
void X64ExecutionEngine::resetCodeCache() { impl->clearCache(); ++impl->counters.cacheResets; }
size_t X64ExecutionEngine::run(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) {
    if(timeline.registerClocks) {
        const size_t steps=impl->reference.run(timeline,budget,stopTicks);
        impl->counters.fallbackSteps+=steps; return steps;
    }
    size_t steps=0;
    uint64_t eventDeadline=timeline.elapsed.ticks;
    impl->eventWindowDirty=true;
    while(steps<budget && !state().halted && !state().waitingForInterrupt && timeline.elapsed.ticks<stopTicks) {
        if(impl->eventWindowDirty || timeline.elapsed.ticks>=eventDeadline) {
            eventDeadline=timeline.elapsed.ticks+std::min(impl->bus.ticksUntilEvent(),stopTicks-timeline.elapsed.ticks);
            impl->eventWindowDirty=false;
        }
        const auto exit=runUntilDeadline(timeline,budget-steps,stopTicks,eventDeadline);
        steps+=exit.steps;
        if(!exit.steps) break;
    }
    return steps;
}
ExecutionExit X64ExecutionEngine::runToBoundary(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) {
    const uint64_t deadline=timeline.elapsed.ticks<stopTicks?
        timeline.elapsed.ticks+std::min(impl->bus.ticksUntilEvent(),stopTicks-timeline.elapsed.ticks):stopTicks;
    return runUntilDeadline(timeline,budget,stopTicks,deadline);
}
ExecutionExit X64ExecutionEngine::runUntilDeadline(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks,uint64_t eventDeadline) {
    auto& engine=*impl;
    const auto& state=engine.cpu.getState();
    // Complex entry conditions and the experimental clock model retain the
    // scalar contract. The legacy title hook is excluded from native fixtures.
    if (!budget || state.halted || state.waitingForInterrupt || timeline.registerClocks ||
        timeline.elapsed.ticks>=stopTicks || engine.bus.wadeTitleBlitEnabled() ||
        (engine.bus.hasPendingIrq() && !(state.cpsr&FLAG_I)) || !engine.cpu.canHandoff())
        return engine.fallback(timeline,budget,stopTicks);
    const uint64_t remaining=eventDeadline-timeline.elapsed.ticks;
    size_t count=std::min<size_t>(budget,32);
    if (engine.bus.needsLevelInterruptRefresh()) count=1;
    if (remaining<=count) count=std::min<uint64_t>(count,legacyStepsUntilTickLimit(remaining,timeline.ratio,timeline.phase));
    auto entry=engine.reusable(count);
    if (!entry) {
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
            const size_t nativeCount=block.instructions.size();
            if (!nativeCount && source.empty()) return engine.fallback(timeline,budget,stopTicks);
            // A safe, fetched unsupported opcode caches a scalar exit. Never
            // cache an unavailable/device fetch or a guest prefetch fault.
            source.resize(nativeCount?nativeCount:1);
            entry=engine.findOrCompile(block,source);
            if (!entry) { ++engine.counters.compilationFailures; return engine.fallback(timeline,budget,stopTicks); }
        } catch (const std::bad_alloc&) {
            ++engine.counters.compilationFailures;
            return engine.fallback(timeline,budget,stopTicks);
        }
    }
    if(!entry->function && entry->nativeEligible) {
        if(++entry->visits>=engine.limits.compileThreshold) {
            try { engine.promote(*entry); }
            catch(const std::bad_alloc&) { ++engine.counters.compilationFailures; entry->visits=0; }
        }
        if(!entry->function) {
            engine.eventWindowDirty=true;
            const auto exit=engine.reference.runToBoundary(timeline,std::min<size_t>(budget,entry->context[5]),stopTicks);
            engine.counters.fallbackSteps+=exit.steps; engine.counters.coldBlockSteps+=exit.steps;
            return exit;
        }
    }
    if(!entry->function) return engine.fallback(timeline,budget,stopTicks);
    // Memory guards may return before the statically decoded end. Device/fault
    // paths have ticked their prefix; ordinary RAM retains one bounded batch.
    const auto startTicks=timeline.elapsed.ticks;
    NativeRun run{&timeline,&engine.architecturalState,entry->source.front().first,uint32_t(count)};
    const uint32_t status=entry->function(&engine.architecturalState,&run);
    if(status) engine.eventWindowDirty=true;
    const size_t nativeCount=run.retired;
    if(nativeCount>entry->source.size()) engine.counters.internalBackedges+=(nativeCount-1)/entry->source.size();
    const size_t tail=nativeCount-run.tickedSteps;
    if(tail) advanceIntegerPrefix(&engine.bus,&timeline,uint32_t(tail));
    const uint64_t ticks=timeline.elapsed.ticks-startTicks;
    engine.counters.generatedSteps+=nativeCount;
    engine.counters.memorySteps+=run.memorySteps;
    if (status&1) return {ExecutionExitReason::CpuEvent,nativeCount,ticks,CpuStepEvent::DataAbort};
    const auto reason=ticks>=remaining?ExecutionExitReason::Deadline:
        (nativeCount==budget?ExecutionExitReason::Budget:ExecutionExitReason::BlockEnd);
    return {reason,nativeCount,ticks,std::nullopt};
}
} // namespace oceanblast
