#include "x64_execution_engine.h"
#include "translation_frontend.h"
#include "arm920t_cycles.h"
#include <asmjit/x86.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#include <cstdlib>
#endif
#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <array>
#include <vector>
#include <utility>
#include <new>
#include <list>

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
    uint32_t base=0;
    uint32_t retired=0,tickedSteps=0,memorySteps=0,inlineLoads=0,loopTrips=0;
    uint32_t cycles=0,lastCycles=1,tickedCycles=0,frequency=0;
    void* exit=nullptr;
    void* link=nullptr;
    void* current=nullptr;
    uintptr_t stack=0;
    uint8_t linkWindow=0;
};
void advanceIntegerPrefix(Bus* bus,ExecutionTimeline* timeline,uint32_t count) {
    // Native windows contain at most 65,536 steps; the scheduler bounds ratios
    // to 1..16. One quotient/remainder pair therefore fits in 32 bits. Ratio 6
    // is a shared legacy setting and uses compiler-generated constant division.
    const uint32_t total=uint32_t(timeline->phase)+count;
    const uint32_t ratio=uint32_t(timeline->ratio);
    const uint32_t quotient=ratio==6?total/6:total/ratio;
    const uint32_t remainder=total-quotient*ratio;
    const uint32_t ticks=quotient+uint32_t(timeline->phase==0)-uint32_t(remainder==0);
    timeline->phase=remainder;
    bus->tick(ticks); timeline->elapsed.ticks+=ticks;
}
// Entry is bounded by the first device event. Ordinary RAM cannot arm a new
// event or change execution context, so its ticks can be committed together.
// Before a device access or fault, commit all preceding instructions first.
void materializeBeforeMemory(Bus* bus,NativeRun* run) {
    const uint32_t completed=run->retired-1;
    if(run->timeline->registerClocks) {
        const uint32_t cycles=run->cycles-run->lastCycles;
        if(cycles>run->tickedCycles) {
            const auto pc=run->state->r[15];run->state->r[15]=pc-4;
            bus->tick(run->timeline->clock.advance(cycles-run->tickedCycles,run->frequency));
            run->timeline->elapsed.ticks=run->timeline->clock.elapsedTicks();
            run->state->r[15]=pc;run->tickedCycles=cycles;run->tickedSteps=completed;
        }
        return;
    }
    if(completed>run->tickedSteps) {
        const auto pc=run->state->r[15]; run->state->r[15]=pc-4;
        advanceIntegerPrefix(bus,run->timeline,completed-run->tickedSteps);
        run->state->r[15]=pc; run->tickedSteps=completed;
    }
}
bool ordinaryRam(uint32_t physical) {
    return physical<ADDR_STEPPINGSTONE_SIZE || physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE;
}
template<unsigned Width> uint64_t checkedLoad(Bus* bus,NativeRun* run,uint32_t address) {
    ++run->memorySteps;
    uint32_t physical=0;
    if(!bus->translateDataAddress(Width==4?address&~3u:address,false,physical)) {
        materializeBeforeMemory(bus,run); return uint64_t(1)<<32;
    }
    // CLKCON readback is a stored value with no read side effects. The
    // kernel's idle loop can read it within the existing device deadline.
    const bool deviceBoundary=!ordinaryRam(physical) && (Width!=4 || physical!=ADDR_CLKCON_BASE+0x0c);
    if(deviceBoundary) materializeBeforeMemory(bus,run);
    uint32_t value=Width==1?bus->read8Phys(physical):(Width==2?bus->read16Phys(physical):bus->read32Phys(physical));
    if (Width==4 && (address&3u)) { const unsigned shift=(address&3u)*8; value=(value>>shift)|(value<<(32-shift)); }
    return (uint64_t(deviceBoundary?2:0)<<32)|value;
}
template<unsigned Width> uint32_t checkedStore(Bus* bus,NativeRun* run,uint32_t address,uint32_t value) {
    ++run->memorySteps;
    uint32_t physical=0;
    if(!bus->translateDataAddress(Width==4?address&~3u:address,true,physical)) {
        materializeBeforeMemory(bus,run); return 1;
    }
    const bool ram=ordinaryRam(physical);
    if(!ram) materializeBeforeMemory(bus,run);
    if (Width==1) bus->write8Phys(physical,uint8_t(value));
    else if(Width==2) bus->write16Phys(physical,uint16_t(value));
    else bus->write32Phys(physical,value);
    if(!ram) return 2;
    // A store to the currently executing physical code page must return before
    // any following translated instruction, including writes through aliases.
    const uint32_t activePage=run->physical&~1023u;
    return (physical&~1023u)==activePage || ((physical+Width-1)&~1023u)==activePage?4:0;
}
void checkedAbort(ARM920T* cpu) { cpu->completeCheckedDataAbort(); }
uint32_t checkedMultiple(Bus* bus,ARM920T* cpu,NativeRun* run,uint32_t encoding) {
    ++run->memorySteps;
    // Commit the integer prefix before any partial register/memory update.
    // A later word can fault or access a device after earlier words succeed.
    materializeBeforeMemory(bus,run);
    auto& state=*run->state;
    const unsigned baseRegister=(encoding>>16)&15;
    const uint32_t registers=encoding&0xffffu,base=state.r[baseRegister];
    const unsigned count=__builtin_popcount(registers);
    const bool load=encoding&(1u<<20),up=encoding&(1u<<23),pre=encoding&(1u<<24);
    uint32_t address=up?base:base-count*4;
    if(pre==up) address+=4;
    uint32_t status=0;
    for(uint32_t list=registers;list;list&=list-1,address+=4) {
        const unsigned reg=__builtin_ctz(list);
        uint32_t physical=0;
        if(!bus->translateDataAddress(address,!load,physical)) { checkedAbort(cpu);return 1; }
        if(!ordinaryRam(physical)) status|=2;
        if(load) {
            const uint32_t value=bus->read32Phys(physical);
            state.r[reg]=reg==15?value&~3u:value;
        } else {
            bus->write32Phys(physical,reg==15?state.r[15]+4:state.r[reg]);
            const uint32_t active=run->physical&~1023u;
            if((physical&~1023u)==active || ((physical+3)&~1023u)==active) status|=4;
        }
    }
    if((encoding&(1u<<21)) && (!load || !(registers&(1u<<baseRegister))))
        state.r[baseRegister]=up?base+count*4:base-count*4;
    return status;
}
bool queryExecutableRegion(const void* address,size_t bytes,uintptr_t& regionBase,size_t& regionSize) {
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION information{};
    if(!VirtualQuery(address,&information,sizeof(information)) ||
        information.State!=MEM_COMMIT || (information.Protect&0xffu)!=PAGE_EXECUTE_READ) return false;
    const auto base=reinterpret_cast<uintptr_t>(information.BaseAddress);
    const auto start=reinterpret_cast<uintptr_t>(address);
    if(start<base || bytes>information.RegionSize || start-base>information.RegionSize-bytes) return false;
    regionBase=base;
    regionSize=information.RegionSize;
    return true;
#else
    FILE* maps=std::fopen("/proc/self/maps","r");
    if(!maps) return false;
    const auto target=reinterpret_cast<uintptr_t>(address);
    char line[512];
    bool ok=false;
    while(std::fgets(line,sizeof(line),maps)) {
        char* cursor=nullptr;
        const uintptr_t start=static_cast<uintptr_t>(std::strtoull(line,&cursor,16));
        if(!cursor || *cursor!='-') continue;
        const uintptr_t end=static_cast<uintptr_t>(std::strtoull(cursor+1,&cursor,16));
        if(!cursor || *cursor!=' ') continue;
        const char* perms=cursor+1;
        if(target>=start && bytes<=end-start && target-start<=end-start-bytes) {
            // Accept dual-mapped r-xp; keep strict non-zero execute bit check.
            ok=(perms[0]=='r' && perms[1]=='-' && perms[2]=='x');
            if(ok) {
                regionBase=start;
                regionSize=end-start;
            }
            break;
        }
    }
    std::fclose(maps);
    return ok;
#endif
}
bool executableReadOnly(const void* address) {
    uintptr_t base=0;
    size_t size=0;
    return queryExecutableRegion(address,1,base,size);
}
}
struct X64ExecutionEngine::Impl {
    using Function=uint32_t(*)(ARM920TState*,NativeRun*);
    using EntryBridge=uint32_t(*)(ARM920TState*,NativeRun*,void*);
    ARM920TState& architecturalState;
    ARM920T& cpu;
    Bus& bus;
    InterpreterEngine reference;
    asmjit::JitAllocator::CreateParams allocation=allocationParameters();
    asmjit::JitRuntime runtime{&allocation};
    EntryBridge enterBridge=nullptr;
    NativeExecutionStats counters;
    struct CacheEntry;
    using Cache=std::list<CacheEntry>;
    struct SuccessorSlot {
        CacheEntry* entry=nullptr;
        uint64_t lifetime=0;
        size_t limit=0;
    };
    struct CacheEntry {
        std::array<uint32_t,6> context;
        std::vector<std::pair<uint32_t,uint32_t>> source;
        Function function;
        size_t bytes;
        uint64_t used;
        uint64_t mapping=0,epoch=0;
        uint32_t physical=0;
        uint32_t version=0;
        bool nativeEligible=false;
        size_t visits=0;
        CacheEntry* nextByPc=nullptr;
        std::array<SuccessorSlot,2> successors{};
    };
    Cache cache;
    struct HotEntry { CacheEntry* entry=nullptr; size_t limit=0; };
    static constexpr size_t hotCount=1024;
    static constexpr size_t pcBucketCount=4096;
    std::array<HotEntry,hotCount> hot{};
    struct CompilationSample { std::array<uint32_t,5> context{}; size_t visits=0; };
    std::array<CompilationSample,hotCount> samples{};
    static size_t hotIndex(uint32_t pc) { return ((pc>>1)^(pc>>12))&(hotCount-1); }
    static size_t pcBucketIndex(uint32_t pc) { return ((pc>>1)^(pc>>13))&(pcBucketCount-1); }
    std::array<CacheEntry*,pcBucketCount> cacheByPc{};
    NativeCacheLimits limits;
    uint64_t useCounter=0;
    uint64_t cacheLifetime=1;
    size_t cacheBytes=0;
    uintptr_t verifiedRegionBase=0;
    size_t verifiedRegionSize=0;
    bool eventWindowDirty=true;
    bool cycleMode=false;
    void selectCycleMode(bool clocks) {
        if(cycleMode==clocks) return;
        clearCache(); cycleMode=clocks; eventWindowDirty=true;
    }
    size_t clockedLimit(const ExecutionTimeline& timeline,size_t wanted,uint64_t remaining,uint32_t frequency) const {
        wanted=std::min<size_t>(wanted,65536);
        if(timeline.clock.ticksForCycles(uint32_t(wanted)*ARM920TCycles::branch,frequency)<remaining) return wanted;
        size_t low=0,high=wanted;
        while(low<high) {
            const size_t mid=(low+high+1)/2;
            if(timeline.clock.ticksForCycles(uint32_t(mid)*ARM920TCycles::branch,frequency)<remaining) low=mid;
            else high=mid-1;
        }
        return low;
    }
    void restrictClockedBlock(Block& block) const {
        if(!cycleMode) return;
        const auto stop=std::find_if(block.instructions.begin(),block.instructions.end(),[](const Instruction& instruction) {
            return instruction.multiple ||
                (!instruction.branch && instruction.operation.opcode==IntegerOpcode::Multiply);
        });
        if(stop==block.instructions.end()) return;
        block.stopPc=stop->pc; block.stopEncoding=stop->encoding; block.stop=BlockStop::Unsupported;
        block.instructions.erase(stop,block.instructions.end());
    }
    bool buildEntryBridge() {
        using namespace asmjit;
        EmitErrors errors;
        CodeHolder bridge;
        if(bridge.init(runtime.environment())!=Error::kOk) return false;
        bridge.set_error_handler(&errors);
        x86::Assembler a(&bridge);
        const auto linkLabel=a.new_label(),zeroExitLabel=a.new_label(),exitLabel=a.new_label();
        // Outer ABI:
        // - Windows x64: RCX state, RDX run, R8 first block, 32-byte shadow space (+ 8 align = 40).
        // - System-V AMD64: RDI state, RSI run, RDX first block, 16-byte stack align (+ 16 spill = 24).
        // Shared internal ABI: R12 state, R13 run, R14d base, R15d cycles, EBX lastCycles, EBP CPSR.
#ifdef _WIN32
        constexpr int32_t kBridgeStackReserve=40;
        const x86::Gp abiArg0=x86::rcx;
        const x86::Gp abiArg1=x86::rdx;
        const x86::Gp abiArg2=x86::r8;
#else
        constexpr int32_t kBridgeStackReserve=24;
        const x86::Gp abiArg0=x86::rdi;
        const x86::Gp abiArg1=x86::rsi;
        const x86::Gp abiArg2=x86::rdx;
#endif
        a.push(x86::rbx);
        a.push(x86::rbp);
        a.push(x86::rsi);
        a.push(x86::rdi);
        a.push(x86::r12);
        a.push(x86::r13);
        a.push(x86::r14);
        a.push(x86::r15);
        a.sub(x86::rsp,kBridgeStackReserve);
        a.mov(x86::r12,abiArg0);
        a.mov(x86::r13,abiArg1);
        a.mov(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,stack))),x86::rsp);
        a.lea(x86::rax,x86::ptr(exitLabel));
        a.mov(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,exit))),x86::rax);
        a.lea(x86::rax,x86::ptr(linkLabel));
        a.mov(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,link))),x86::rax);
        a.cmp(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,limit))),0);
        a.je(zeroExitLabel);
        a.jmp(abiArg2);

        a.bind(linkLabel);
        a.mov(x86::eax,x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,retired))));
        a.cmp(x86::eax,x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,limit))));
        a.jae(zeroExitLabel);
        a.cmp(x86::byte_ptr(x86::r13,int32_t(offsetof(NativeRun,linkWindow))),0);
        a.je(zeroExitLabel);
        a.mov(abiArg0,uintptr_t(this));
        a.mov(abiArg1,x86::r13);
        a.mov(x86::rax,uintptr_t(&Impl::stepWindow));
        a.call(x86::rax);
        a.test(x86::rax,x86::rax);
        a.jz(zeroExitLabel);
        a.jmp(x86::rax);

        a.bind(zeroExitLabel);
        a.xor_(x86::eax,x86::eax);
        a.bind(exitLabel);
        a.add(x86::rsp,kBridgeStackReserve);
        a.pop(x86::r15);
        a.pop(x86::r14);
        a.pop(x86::r13);
        a.pop(x86::r12);
        a.pop(x86::rdi);
        a.pop(x86::rsi);
        a.pop(x86::rbp);
        a.pop(x86::rbx);
        a.ret();
        if(errors.error!=Error::kOk || runtime.add(&enterBridge,&bridge)!=Error::kOk) return false;
        if(!executableReadOnly(reinterpret_cast<const void*>(enterBridge))) {
            runtime.release(enterBridge); enterBridge=nullptr; return false;
        }
        return true;
    }
    Impl(ARM920TState& state,ARM920T& cpu,Bus& bus,NativeCacheLimits limits) : architecturalState(state),cpu(cpu),bus(bus),reference(cpu,bus),limits(limits) {
        if (&state!=&cpu.getState()) throw std::invalid_argument("Native and reference CPU state must match");
        if (!limits.blocks || !limits.bytes || !limits.compileThreshold) throw std::invalid_argument("Native cache limits must be positive");
        if (!buildEntryBridge()) throw std::runtime_error("Cannot initialize shared native entry bridge");
    }
    ~Impl() { clearCache(); if(enterBridge) runtime.release(enterBridge); }
    void refreshDependency(CacheEntry& entry) {
        entry.physical=entry.source.front().first;
        entry.mapping=bus.getMappingGeneration(entry.context[0]); entry.epoch=bus.getCodeEpoch();
        entry.version=bus.markCodePage(entry.physical);
    }
    void clearCache() {
        ++cacheLifetime;
        hot.fill({});
        samples.fill({});
        for (const auto& entry : cache) if(entry.function) runtime.release(entry.function);
        cacheByPc.fill(nullptr); cache.clear(); cacheBytes=0;
        verifiedRegionBase=0; verifiedRegionSize=0;
    }
    bool verifyExecutableSpan(const void* address,size_t bytes) {
        const auto start=reinterpret_cast<uintptr_t>(address);
        if(verifiedRegionSize && start>=verifiedRegionBase && bytes<=verifiedRegionSize &&
            start-verifiedRegionBase<=verifiedRegionSize-bytes) return true;
        return queryExecutableRegion(address,bytes,verifiedRegionBase,verifiedRegionSize);
    }
    bool sampledHotEntry() {
        const auto& state=architecturalState;
        const std::array<uint32_t,5> context={state.r[15],(state.cpsr&0x3fu)|(bus.isUserMode()?0x100u:0u),
            state.cp15_control,state.cp15_ttb,state.cp15_dacr};
        auto& sample=samples[hotIndex(context[0])];
        if(!sample.visits || sample.context!=context) {
            for(auto* candidate=cacheByPc[pcBucketIndex(context[0])];candidate;candidate=candidate->nextByPc) {
                const auto& key=candidate->context;
                if(candidate->function && key[0]==context[0] && key[1]==context[1] &&
                    key[2]==context[2] && key[3]==context[3] && key[4]==context[4]) {
                    sample={context,limits.compileThreshold};
                    return true;
                }
            }
            sample.context=context; sample.visits=1;
        }
        else if(sample.visits<limits.compileThreshold) ++sample.visits;
        return sample.visits>=limits.compileThreshold;
    }

    Function publish(asmjit::CodeHolder& code,size_t& bytes) {
        bytes=code.code_size();
        if (!bytes || bytes>64*1024) return nullptr;
        Function result=nullptr;
        if (runtime.add(&result,&code)!=asmjit::Error::kOk) return nullptr;
        if (!verifyExecutableSpan(reinterpret_cast<const void*>(result),bytes)) { runtime.release(result); return nullptr; }
        ++counters.compilations;
        return result;
    }
    void emitMemory(asmjit::x86::Assembler& a,const Instruction& instruction) {
        using namespace asmjit;
        const auto& memory=*instruction.memory;
        const unsigned width=memory.byte?1:(memory.halfword?2:4);
        auto reg=[&](unsigned index) { return x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+index*4)); };
        a.mov(reg(15),instruction.pc+4);
        if(memory.base==15) a.mov(x86::eax,instruction.pc+8);
        else a.mov(x86::eax,reg(memory.base));
        a.mov(x86::esi,x86::eax);
        if(memory.offsetRegister) {
            a.mov(x86::ecx,reg(*memory.offsetRegister));
            switch(memory.offsetShift) {
                case Shift::Lsl:if(memory.offsetShiftAmount) a.shl(x86::ecx,memory.offsetShiftAmount);break;
                case Shift::Lsr:if(memory.offsetShiftAmount==32) a.xor_(x86::ecx,x86::ecx);else a.shr(x86::ecx,memory.offsetShiftAmount);break;
                case Shift::Asr:a.sar(x86::ecx,std::min<unsigned>(memory.offsetShiftAmount,31));break;
                case Shift::Ror:a.ror(x86::ecx,memory.offsetShiftAmount);break;
                case Shift::Rrx:
                    a.mov(x86::edx,x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,cpsr))));
                    a.bt(x86::edx,29); a.rcr(x86::ecx,1); break;
            }
            if(memory.up) a.add(x86::esi,x86::ecx); else a.sub(x86::esi,x86::ecx);
        } else if(memory.offset) {
            if(memory.up) a.add(x86::esi,memory.offset); else a.sub(x86::esi,memory.offset);
        }
        a.mov(x86::r8d,memory.pre?x86::esi:x86::eax);
        if (memory.load) {
            const auto slow=a.new_label(),translated=a.new_label(),sram=a.new_label(),loaded=a.new_label(),complete=a.new_label();
            const auto view=bus.nativeReadView();
            a.mov(x86::edx,x86::r8d); if(width==4) a.and_(x86::edx,~3u);
            a.mov(x86::rax,uintptr_t(view.enabled)); a.cmp(x86::byte_ptr(x86::rax),0); a.je(translated);
            a.mov(x86::ecx,x86::edx); a.shr(x86::ecx,12); a.mov(x86::r9d,x86::ecx); a.and_(x86::r9d,uint32_t(view.size-1));
            a.mov(x86::rax,uintptr_t(view.entries)); a.lea(x86::rax,x86::ptr(x86::rax,x86::r9,3));
            a.mov(x86::r10d,x86::dword_ptr(x86::rax,int32_t(offsetof(Arm920TMmu::TlbEntry,tag))));
            a.mov(x86::r11d,x86::r10d); a.and_(x86::r11d,view.keyMask); a.or_(x86::ecx,view.valid);
            a.cmp(x86::r11d,x86::ecx); a.jne(slow);
            const auto permitted=a.new_label();
            a.mov(x86::rcx,uintptr_t(view.user));
            a.cmp(x86::byte_ptr(x86::rcx),0); a.je(permitted);
            a.mov(x86::ecx,x86::edx); a.shr(x86::ecx,10); a.and_(x86::ecx,3);
            a.shl(x86::ecx,1); a.add(x86::ecx,view.apShift); a.shr(x86::r10d,x86::cl); a.and_(x86::r10d,3);
            a.cmp(x86::r10d,1); a.jbe(slow);
            a.bind(permitted); a.and_(x86::edx,4095);
            a.or_(x86::edx,x86::dword_ptr(x86::rax,int32_t(offsetof(Arm920TMmu::TlbEntry,paBase))));
            a.bind(translated);
            a.mov(x86::r9d,x86::edx); a.sub(x86::r9d,ADDR_SDRAM_BASE);
            a.cmp(x86::r9d,ADDR_SDRAM_SIZE-(width-1)); a.jae(sram);
            a.mov(x86::rax,uintptr_t(bus.getSdramPtr())); a.jmp(loaded);
            a.bind(sram); a.cmp(x86::edx,ADDR_STEPPINGSTONE_SIZE-(width-1)); a.jae(slow);
            a.mov(x86::r9d,x86::edx); a.mov(x86::rax,uintptr_t(bus.getSteppingstonePtr()));
            a.bind(loaded);
            if(width==1) a.movzx(x86::eax,x86::byte_ptr(x86::rax,x86::r9));
            else if(width==2) a.movzx(x86::eax,x86::word_ptr(x86::rax,x86::r9));
            else {
                a.mov(x86::eax,x86::dword_ptr(x86::rax,x86::r9));
                a.mov(x86::ecx,x86::r8d); a.and_(x86::ecx,3); a.shl(x86::ecx,3);
                a.ror(x86::eax,x86::cl);
            }
            a.xor_(x86::edx,x86::edx);
            a.inc(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,memorySteps))));
            a.inc(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,inlineLoads))));
            a.jmp(complete); a.bind(slow);
#ifdef _WIN32
            a.mov(x86::rcx,uintptr_t(&bus));
            a.mov(x86::rdx,x86::r13);
#else
            if(memory.writeback) a.mov(x86::dword_ptr(x86::rsp),x86::esi);
            a.mov(x86::rdi,uintptr_t(&bus));
            a.mov(x86::rsi,x86::r13);
            a.mov(x86::edx,x86::r8d);
#endif
            a.mov(x86::rax,uintptr_t(width==1?(void*)checkedLoad<1>:(width==2?(void*)checkedLoad<2>:(void*)checkedLoad<4>)));
            a.call(x86::rax);
#ifndef _WIN32
            if(memory.writeback) a.mov(x86::esi,x86::dword_ptr(x86::rsp));
#endif
            a.mov(x86::rdx,x86::rax); a.shr(x86::rdx,32);
            a.bind(complete);
            if(memory.signExtend) {
                if(width==1) a.movsx(x86::eax,x86::al); else a.movsx(x86::eax,x86::ax);
            }
        } else {
#ifdef _WIN32
            a.mov(x86::rcx,uintptr_t(&bus));
            a.mov(x86::rdx,x86::r13);
            a.mov(x86::r9d,reg(memory.destination));
#else
            if(memory.writeback) a.mov(x86::dword_ptr(x86::rsp),x86::esi);
            a.mov(x86::rdi,uintptr_t(&bus));
            a.mov(x86::rsi,x86::r13);
            a.mov(x86::edx,x86::r8d);
            a.mov(x86::ecx,reg(memory.destination));
#endif
            a.mov(x86::rax,uintptr_t(width==1?(void*)checkedStore<1>:(width==2?(void*)checkedStore<2>:(void*)checkedStore<4>)));
            a.call(x86::rax);
#ifndef _WIN32
            if(memory.writeback) a.mov(x86::esi,x86::dword_ptr(x86::rsp));
#endif
            a.mov(x86::edx,x86::eax);
        }
        const auto abort=a.new_label(),done=a.new_label();
        a.test(x86::edx,1); a.jnz(abort);
        if (memory.load) a.mov(reg(memory.destination),x86::eax);
        if (memory.writeback) a.mov(reg(memory.base),x86::esi);
        a.test(x86::edx,x86::edx); a.je(done);
        a.mov(x86::eax,x86::edx);
        a.jmp(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,exit))));
        a.bind(abort);
#ifdef _WIN32
        a.mov(x86::rcx,uintptr_t(&cpu));
#else
        a.mov(x86::rdi,uintptr_t(&cpu));
#endif
        a.mov(x86::rax,uintptr_t(&checkedAbort));
        a.call(x86::rax);
        a.mov(x86::eax,1);
        a.jmp(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,exit))));
        a.bind(done);
    }
    Function compile(const Block& block,size_t count,size_t& bytes) {
        using namespace asmjit;
        static_assert(std::is_standard_layout<ARM920TState>::value,"JIT register offsets require standard layout");
        EmitErrors errors;
        CodeHolder code;
        if (code.init(runtime.environment())!=Error::kOk) return nullptr;
        code.set_error_handler(&errors);
        x86::Assembler a(&code);
        static constexpr std::array<x86::Gp,4> slotRegs={x86::esi,x86::edi,x86::r10d,x86::r11d};
        struct RegSlot { uint8_t guest=0xff; bool dirty=false; uint32_t used=0; };
        std::array<RegSlot,4> slots{};
        uint32_t slotTick=0;
        uint8_t pinnedMask=0;
        bool flagsValid=false,flagsDirty=false;
        auto readFlags=[&]() {
            if(!flagsValid) {
                a.mov(x86::ebp,x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,cpsr))));
                flagsValid=true;
            }
            return x86::ebp;
        };
        auto ensureSlot=[&](unsigned index,bool loadExisting) {
            for(unsigned s=0;s<slots.size();++s) {
                if(slots[s].guest==index) {
                    pinnedMask|=uint8_t(1u<<s);
                    slots[s].used=++slotTick;
                    return s;
                }
            }
            unsigned chosen=0;
            bool haveChosen=false;
            for(unsigned s=0;s<slots.size();++s) {
                if(pinnedMask&(1u<<s)) continue;
                if(!haveChosen || slots[s].used<slots[chosen].used) { chosen=s; haveChosen=true; }
            }
            if(slots[chosen].guest!=0xff && slots[chosen].dirty)
                a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+slots[chosen].guest*4)),slotRegs[chosen]);
            if(loadExisting)
                a.mov(slotRegs[chosen],x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+index*4)));
            slots[chosen]={uint8_t(index),false,++slotTick};
            pinnedMask|=uint8_t(1u<<chosen);
            return chosen;
        };
        auto readRegister=[&](unsigned index) {
            for(unsigned s=0;s<slots.size();++s)
                if(slots[s].guest==index) return slotRegs[s];
            return slotRegs[ensureSlot(index,true)];
        };
        auto flushRegisters=[&]() {
            for(unsigned s=0;s<slots.size();++s) {
                if(slots[s].guest!=0xff && slots[s].dirty)
                    a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+slots[s].guest*4)),slotRegs[s]);
                slots[s]={};
            }
            if(flagsDirty) a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,cpsr))),x86::ebp);
            flagsValid=false; flagsDirty=false; pinnedMask=0;
        };
        auto loadInto=[&](x86::Gp target,Value value) {
            if (value.kind==ValueKind::Register)
                a.mov(target,readRegister(value.value));
            else if(value.value==0) a.xor_(target,target);
            else a.mov(target,value.value);
        };
        Label blockExit=a.new_label(),blockStart=a.new_label();
        const auto& terminal=block.instructions[count-1];
        size_t loopEntry=count;
        if(terminal.branch && !terminal.branch->link && !terminal.branch->targetRegister) {
            for(size_t index=0;index<count;++index)
                if(block.instructions[index].pc==terminal.branch->target) { loopEntry=index; break; }
        }
        const bool repeatable=loopEntry<count;
        const uint32_t loopLength=uint32_t(count-loopEntry);
        if(cycleMode) a.mov(x86::r15d,x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,cycles))));
        a.mov(x86::r14d,x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,base))));
        for (size_t index=0;index<count;++index) {
            if(index==(repeatable?loopEntry:0)) {
                if(index) flushRegisters();
                a.bind(blockStart);
            }
            const auto& instruction=block.instructions[index];
            const auto& op=instruction.operation;
            pinnedMask=0;
            unsigned destSlot=0;
            // Helpers and control-flow exits observe authoritative registers.
            // Predicated destinations start with their old value so a skipped
            // operation can share the same cached register at the merge point.
            if(instruction.memory || instruction.multiple || instruction.branch) flushRegisters();
            else {
                if(op.left.kind==ValueKind::Register) ensureSlot(op.left.value,true);
                if(op.right.kind==ValueKind::Register) ensureSlot(op.right.value,true);
                if(op.addend) ensureSlot(*op.addend,true);
                if(op.shiftRegister) ensureSlot(*op.shiftRegister,true);
                if(op.destination) destSlot=ensureSlot(*op.destination,instruction.condition!=Condition::Always);
            }
            const bool logicalFlags=op.flags==FlagWrite::Logical;
            const bool usesCarry=op.opcode==IntegerOpcode::AddCarry || op.opcode==IntegerOpcode::SubtractCarry ||
                op.opcode==IntegerOpcode::ReverseSubtractCarry || op.shift==Shift::Rrx;
            if(instruction.condition!=Condition::Always || op.flags!=FlagWrite::Preserve || usesCarry)
                readFlags();
            if(cycleMode) {
                a.inc(x86::r15d);
                a.mov(x86::ebx,ARM920TCycles::failedCondition);
            }
            Label skipped=a.new_label();
            if (instruction.condition!=Condition::Always) {
                auto skipBit=[&](uint32_t mask,bool whenSet) {
                    a.test(x86::ebp,mask);
                    if(whenSet) a.jnz(skipped); else a.jz(skipped);
                };
                auto testNvDifference=[&]() {
                    a.mov(x86::eax,x86::ebp);
                    a.shl(x86::eax,3); a.xor_(x86::eax,x86::ebp); a.test(x86::eax,FLAG_N);
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
                    case Condition::Hi:case Condition::Ls:
                        a.mov(x86::eax,x86::ebp); a.and_(x86::eax,FLAG_C|FLAG_Z);
                        a.cmp(x86::eax,FLAG_C);
                        if(instruction.condition==Condition::Hi) a.jne(skipped); else a.je(skipped);
                        break;
                    case Condition::Ge:testNvDifference();a.jnz(skipped);break;
                    case Condition::Lt:testNvDifference();a.jz(skipped);break;
                    case Condition::Gt:skipBit(FLAG_Z,true);testNvDifference();a.jnz(skipped);break;
                    case Condition::Le: {
                        const auto passed=a.new_label(); a.test(x86::ebp,FLAG_Z); a.jnz(passed);
                        testNvDifference(); a.jz(skipped); a.bind(passed); break;
                    }
                    case Condition::Always:break;
                }
            }
            if(cycleMode) {
                const uint32_t cycles=instruction.memory?ARM920TCycles::singleMemory:
                    (instruction.branch?ARM920TCycles::branch:ARM920TCycles::integer(bool(op.shiftRegister)));
                if(cycles>1) a.add(x86::r15d,cycles-1);
                a.mov(x86::ebx,cycles);
            }
            if(instruction.memory || instruction.multiple) {
                a.lea(x86::eax,x86::ptr(x86::r14,int32_t(index+1)));
                a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,retired))),x86::eax);
                if(cycleMode) {
                    a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,cycles))),x86::r15d);
                    a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,lastCycles))),x86::ebx);
                }
                if(instruction.multiple) {
                    a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+15*4)),instruction.pc+4);
#ifdef _WIN32
                    a.mov(x86::rcx,uintptr_t(&bus));
                    a.mov(x86::rdx,uintptr_t(&cpu));
                    a.mov(x86::r8,x86::r13);
                    a.mov(x86::r9d,instruction.encoding);
#else
                    a.mov(x86::rdi,uintptr_t(&bus));
                    a.mov(x86::rsi,uintptr_t(&cpu));
                    a.mov(x86::rdx,x86::r13);
                    a.mov(x86::ecx,instruction.encoding);
#endif
                    a.mov(x86::rax,uintptr_t(&checkedMultiple));
                    a.call(x86::rax);
                    const auto next=a.new_label(); a.test(x86::eax,x86::eax); a.je(next);
                    a.jmp(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,exit))));
                    a.bind(next);
                    if(instruction.multiple->load && (instruction.multiple->registers&0x8000u)) a.jmp(blockExit);
                } else emitMemory(a,instruction);
                a.bind(skipped); continue;
            }
            if(instruction.branch) {
                if(instruction.branch->targetRegister) {
                    a.mov(x86::eax,x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+*instruction.branch->targetRegister*4)));
                    a.and_(x86::eax,~3u);
                    a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+15*4)),x86::eax);
                    a.jmp(blockExit);a.bind(skipped);continue;
                }
                if(instruction.branch->link)
                    a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+14*4)),instruction.pc+instruction.bytes);
                a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+15*4)),instruction.branch->target);
                // The frontend follows only unconditional, same-page edges.
                // Their guest instruction and optional LR write still retire.
                if(index+1<count && instruction.condition==Condition::Always &&
                    block.instructions[index+1].pc==instruction.branch->target) {
                    a.bind(skipped); continue;
                }
                if(repeatable) {
                    a.lea(x86::eax,x86::ptr(x86::r14,int32_t(count)));
                    a.lea(x86::ecx,x86::ptr(x86::rax,int32_t(loopLength)));
                    const auto finished=a.new_label();
                    a.cmp(x86::ecx,x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,limit)))); a.ja(finished);
                    a.add(x86::r14d,loopLength);
                    a.inc(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,loopTrips))));
                    a.jmp(blockStart); a.bind(finished); a.mov(x86::r14d,x86::eax);
                }
                a.jmp(blockExit); a.bind(skipped); continue;
            }
            loadInto(x86::edx,op.right);
            const bool unary=op.opcode==IntegerOpcode::Move || op.opcode==IntegerOpcode::Not;
            if(op.shiftRegister) {
                a.mov(x86::ecx,readRegister(*op.shiftRegister));
                a.and_(x86::ecx,255);
                const auto done=a.new_label();
                if(logicalFlags) { a.mov(x86::r9d,x86::ebp); a.shr(x86::r9d,29); a.and_(x86::r9d,1); }
                a.test(x86::ecx,x86::ecx); a.jz(done);
                if(op.shift==Shift::Ror) {
                    a.ror(x86::edx,x86::cl);
                    if(logicalFlags) { a.mov(x86::r9d,x86::edx); a.shr(x86::r9d,31); }
                } else {
                    const auto large=a.new_label(); a.cmp(x86::ecx,32); a.jae(large);
                    if(op.shift==Shift::Lsl) a.shl(x86::edx,x86::cl);
                    else if(op.shift==Shift::Lsr) a.shr(x86::edx,x86::cl);
                    else a.sar(x86::edx,x86::cl);
                    if(logicalFlags) { a.setb(x86::r9b); a.movzx(x86::r9d,x86::r9b); }
                    a.jmp(done); a.bind(large);
                    if(op.shift==Shift::Asr) {
                        if(logicalFlags) { a.mov(x86::r9d,x86::edx); a.shr(x86::r9d,31); }
                        a.sar(x86::edx,31);
                    } else {
                        if(logicalFlags) {
                            const auto clearCarry=a.new_label(); a.xor_(x86::r9d,x86::r9d);
                            a.cmp(x86::ecx,32); a.jne(clearCarry);
                            a.mov(x86::r9d,x86::edx);
                            if(op.shift==Shift::Lsl) a.and_(x86::r9d,1); else a.shr(x86::r9d,31);
                            a.bind(clearCarry);
                        }
                        a.xor_(x86::edx,x86::edx);
                    }
                }
                a.bind(done);
            } else if (op.shifterCarry==CarrySource::Zero) { if(logicalFlags) a.xor_(x86::r9d,x86::r9d); }
            else if (op.shifterCarry==CarrySource::One) { if(logicalFlags) a.mov(x86::r9d,1); }
            else if (op.shifterCarry==CarrySource::Shifter) {
                const unsigned amount=op.shiftAmount;
                if ((op.shift==Shift::Lsr || op.shift==Shift::Asr) && amount==32) {
                    if(logicalFlags) { a.mov(x86::r9d,x86::edx); a.shr(x86::r9d,31); }
                    if (op.shift==Shift::Lsr) a.xor_(x86::edx,x86::edx); else a.sar(x86::edx,31);
                } else {
                    switch(op.shift) {
                        case Shift::Lsl:a.shl(x86::edx,amount);break;
                        case Shift::Lsr:a.shr(x86::edx,amount);break;
                        case Shift::Asr:a.sar(x86::edx,amount);break;
                        case Shift::Ror:a.ror(x86::edx,amount);break;
                        case Shift::Rrx:a.bt(x86::ebp,29);a.rcr(x86::edx,1);break;
                    }
                    if(logicalFlags) { a.setb(x86::r9b); a.movzx(x86::r9d,x86::r9b); }
                }
            } else if(logicalFlags) { a.mov(x86::r9d,x86::ebp); a.shr(x86::r9d,29); a.and_(x86::r9d,1); }
            const bool subtraction=op.opcode==IntegerOpcode::Subtract || op.opcode==IntegerOpcode::ReverseSubtract ||
                op.opcode==IntegerOpcode::SubtractCarry || op.opcode==IntegerOpcode::ReverseSubtractCarry;
            if(unary) a.mov(x86::eax,x86::edx);
            else if(op.opcode==IntegerOpcode::ReverseSubtract || op.opcode==IntegerOpcode::ReverseSubtractCarry) {
                a.mov(x86::eax,x86::edx);
                loadInto(x86::edx,op.left);
            } else loadInto(x86::eax,op.left);
            switch(op.opcode) {
                case IntegerOpcode::And:a.and_(x86::eax,x86::edx);break;
                case IntegerOpcode::Xor:a.xor_(x86::eax,x86::edx);break;
                case IntegerOpcode::Subtract:case IntegerOpcode::ReverseSubtract:a.sub(x86::eax,x86::edx);break;
                case IntegerOpcode::Add:a.add(x86::eax,x86::edx);break;
                case IntegerOpcode::AddCarry:a.bt(x86::ebp,29);a.adc(x86::eax,x86::edx);break;
                case IntegerOpcode::SubtractCarry:case IntegerOpcode::ReverseSubtractCarry:
                    a.bt(x86::ebp,29);a.cmc();a.sbb(x86::eax,x86::edx);break;
                case IntegerOpcode::Or:a.or_(x86::eax,x86::edx);break;
                case IntegerOpcode::Move:break;
                case IntegerOpcode::BitClear:a.not_(x86::edx);a.and_(x86::eax,x86::edx);break;
                case IntegerOpcode::Not:a.not_(x86::eax);break;
                case IntegerOpcode::Multiply:
                    a.imul(x86::eax,x86::edx);
                    if(op.addend) a.add(x86::eax,readRegister(*op.addend));
                    break;
            }
            if (op.flags!=FlagWrite::Preserve) {
                if (op.flags==FlagWrite::Logical) a.test(x86::eax,x86::eax);
                a.sets(x86::cl); a.sete(x86::dl);
                if (op.flags==FlagWrite::Arithmetic) {
                    a.setb(x86::r9b); a.seto(x86::r8b);
                }
                a.movzx(x86::ecx,x86::cl); a.movzx(x86::edx,x86::dl); a.movzx(x86::r9d,x86::r9b);
                if (op.flags==FlagWrite::Arithmetic) {
                    a.movzx(x86::r8d,x86::r8b);
                    if (subtraction) a.xor_(x86::r9d,1);
                    a.and_(x86::ebp,0x0fffffffu);
                    a.shl(x86::r8d,28); a.or_(x86::ebp,x86::r8d);
                } else a.and_(x86::ebp,0x1fffffffu);
                a.shl(x86::ecx,31); a.shl(x86::edx,30); a.shl(x86::r9d,29);
                a.or_(x86::ebp,x86::ecx); a.or_(x86::ebp,x86::edx); a.or_(x86::ebp,x86::r9d);
                flagsDirty=true;
            }
            if (op.destination) {
                a.mov(slotRegs[destSlot],x86::eax); slots[destSlot].dirty=true;
            }
            a.bind(skipped);
        }
        flushRegisters();
        const auto& last=block.instructions[count-1];
        a.mov(x86::dword_ptr(x86::r12,int32_t(offsetof(ARM920TState,r)+15*4)),last.pc+last.bytes);
        if(repeatable) a.add(x86::r14d,uint32_t(count));
        a.bind(blockExit);
        if(!repeatable) a.add(x86::r14d,uint32_t(count));
        a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,retired))),x86::r14d);
        if(cycleMode) {
            a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,cycles))),x86::r15d);
            a.mov(x86::dword_ptr(x86::r13,int32_t(offsetof(NativeRun,lastCycles))),x86::ebx);
        }
        a.jmp(x86::qword_ptr(x86::r13,int32_t(offsetof(NativeRun,link))));
        if (errors.error!=Error::kOk) return nullptr;
        return publish(code,bytes);
    }
    void evictOldest() {
        // Invalidate every borrowed successor before releasing any entry.
        ++cacheLifetime;
        auto oldest=std::min_element(cache.begin(),cache.end(),
            [](const CacheEntry& left,const CacheEntry& right) { return left.used<right.used; });
        auto& slot=hot[hotIndex(oldest->context[0])];
        if(slot.entry==&*oldest) slot={};
        for(auto** link=&cacheByPc[pcBucketIndex(oldest->context[0])];*link;link=&(*link)->nextByPc)
            if(*link==&*oldest) { *link=oldest->nextByPc; break; }
        cacheBytes-=oldest->bytes;
        if(oldest->function) { runtime.release(oldest->function); verifiedRegionBase=0; verifiedRegionSize=0; }
        cache.erase(oldest); ++counters.cacheEvictions;
    }
    void promote(CacheEntry& entry) {
        const auto set=(entry.context[1]&FLAG_T)?InstructionSet::Thumb:InstructionSet::Arm;
        auto fetch=[&](uint32_t address,InstructionSet) {
            const uint32_t physical=entry.physical+(address-entry.context[0]);
            const auto word=std::find_if(entry.source.begin(),entry.source.end(),
                [&](const auto& source) { return source.first==physical; });
            return word!=entry.source.end()?FetchResult{true,word->second}:FetchResult{false,0};
        };
        auto block=decodeBlock(entry.context[0],set,fetch,{entry.context[5],128,limits.followDirectBranches});
        restrictClockedBlock(block);
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
        bool samePcExists=false;
        auto& bucket=cacheByPc[pcBucketIndex(context[0])];
        for (auto* candidate=bucket;candidate;candidate=candidate->nextByPc) {
            auto& entry=*candidate;
            if (entry.context[0]==context[0]) samePcExists=true;
            if (entry.context==context && entry.source==source) {
                refreshDependency(entry);
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
            auto& inserted=cache.back();
            inserted.nativeEligible=!scalar;
            refreshDependency(inserted);
            inserted.nextByPc=bucket;
            bucket=&inserted;
        } catch (...) { if(function) runtime.release(function); throw; }
        cacheBytes+=bytes;
        counters.peakCacheEntries=std::max<uint64_t>(counters.peakCacheEntries,cache.size());
        counters.peakCacheBytes=std::max<uint64_t>(counters.peakCacheBytes,cacheBytes);
        hot[hotIndex(context[0])]={};
        if(samePcExists) ++cacheLifetime;
        if(scalar) ++counters.fallbackEntries;
        return &cache.back();
    }
    CacheEntry* reusable(size_t limit,size_t* resolvedLimit=nullptr) {
        const auto& state=cpu.getState();
        const uint32_t pc=state.r[15],width=(state.cpsr&FLAG_T)?2:4;
        const uint32_t mode=(state.cpsr&0x3fu)|(bus.isUserMode()?0x100u:0u);
        Bus::MmuFault fault=Bus::MmuFault::NONE;
        if (cache.empty()) return nullptr;
        auto sameContext=[&](const CacheEntry& entry) {
            const auto& key=entry.context;
            return key[0]==pc && key[1]==mode && key[2]==state.cp15_control &&
                key[3]==state.cp15_ttb && key[4]==state.cp15_dacr;
        };
        auto eligible=[&](const CacheEntry& entry) {
            return sameContext(entry) && entry.context[5]<=limit;
        };
        const uint64_t currentMapping=bus.getMappingGeneration(pc);
        auto unchanged=[&](const CacheEntry& entry) {
            return entry.mapping==currentMapping && entry.epoch==bus.getCodeEpoch() &&
                entry.version==bus.getCodePageVersion(entry.physical);
        };
        auto touch=[&](CacheEntry& entry,bool fast) {
            entry.used=++useCounter;
            if(entry.function) { ++counters.cacheHits; if(fast) ++counters.fastCacheHits; }
            else if(entry.nativeEligible) ++counters.coldCacheHits;
            else ++counters.fallbackCacheHits;
            return &entry;
        };
        auto& slot=hot[hotIndex(pc)];
        // Match the lookup budget as well: a previously selected short entry
        // must not hide a longer eligible translation on a larger lookup.
        if(slot.entry && limit<=slot.limit && eligible(*slot.entry) && unchanged(*slot.entry)) {
            if(resolvedLimit) *resolvedLimit=slot.limit;
            ++counters.directCacheHits; return touch(*slot.entry,true);
        }
        uint32_t physical=0;
        bool translated=false,bestFast=false;
        size_t maxLimit=32;
        CacheEntry* best=nullptr;
        for (auto* candidate=cacheByPc[pcBucketIndex(pc)];candidate;candidate=candidate->nextByPc) {
            auto& entry=*candidate;
            const auto& key=entry.context;
            if (!sameContext(entry)) continue;
            if (key[5]>limit) {
                maxLimit=std::min<size_t>(maxLimit,key[5]-1);
                continue;
            }
            if (best && key[5]<=best->context[5]) continue;
            if(unchanged(entry)) {
                best=&entry; bestFast=true; continue;
            }
            if(!translated) {
                physical=bus.translate(pc,&fault); translated=true;
                if(fault!=Bus::MmuFault::NONE) return nullptr;
            }
            // Like the reference fetch cache, a native block stays in one 1 KB
            // permission span. One checked translation establishes its mapping;
            // every source word is still checked for self-modifying/alias writes.
            if ((physical&1023u)!=(pc&1023u)) continue;
            const bool ram=physical<ADDR_STEPPINGSTONE_SIZE || physical-ADDR_SDRAM_BASE<ADDR_SDRAM_SIZE;
            if (!ram) continue;
            // A permission-checked translation to the same physical source
            // does not require rereading bytes when RAM/DMA versions agree.
            if(entry.physical==physical && entry.epoch==bus.getCodeEpoch() &&
                entry.version==bus.getCodePageVersion(physical)) {
                entry.mapping=currentMapping; ++counters.mappingRevalidations;
                best=&entry; bestFast=false; continue;
            }
            ++counters.sourceValidations;
            bool valid=true;
            for (size_t index=0;index<entry.source.size();++index) {
                const uint32_t address=(physical&~1023u)|(entry.source[index].first&1023u);
                if (entry.source[index].first!=address || entry.source[index].second!=
                    (width==4?bus.read32Phys(address):uint32_t(bus.read16Phys(address)))) { valid=false; break; }
            }
            if (valid) { refreshDependency(entry); best=&entry; bestFast=false; }
        }
        if (!best) return nullptr;
        maxLimit=std::max(limit,maxLimit);
        const size_t newLimit=slot.entry==best?std::max(slot.limit,maxLimit):maxLimit;
        slot={best,newLimit};
        if(resolvedLimit) *resolvedLimit=newLimit;
        return touch(*best,bestFast);
    }
    ExecutionExit fallback(ExecutionTimeline& timeline,size_t budget,uint64_t deadline) {
        eventWindowDirty=true;
        auto result=reference.runToBoundary(timeline,std::min<size_t>(budget,1),deadline);
        counters.fallbackSteps+=result.steps;
        return result;
    }
    ExecutionExit checkedScalarExit(ExecutionTimeline& timeline,uint64_t deadline) {
        // Native entry guards have already established a runnable instruction
        // and its event window. Execute the same reference step once, without
        // rebuilding a second scalar run loop and event deadline around it.
        eventWindowDirty=true;
        const auto& state=architecturalState;
        const uint32_t cpsr=state.cpsr,control=state.cp15_control,ttb=state.cp15_ttb,dacr=state.cp15_dacr;
        const uint64_t start=timeline.elapsed.ticks;
        reference.step(timeline); ++counters.fallbackSteps;
        const auto event=cpu.getLastEvent();
        const uint64_t ticks=timeline.elapsed.ticks-start;
        if(event!=CpuStepEvent::Instruction) return {ExecutionExitReason::CpuEvent,1,ticks,event};
        if(((cpsr^state.cpsr)&0xffu) || control!=state.cp15_control || ttb!=state.cp15_ttb || dacr!=state.cp15_dacr)
            return {ExecutionExitReason::ContextChange,1,ticks,std::nullopt};
        const auto reason=state.halted?ExecutionExitReason::Halted:
            (timeline.elapsed.ticks>=deadline?ExecutionExitReason::Deadline:
            (state.waitingForInterrupt&&!bus.hasPendingIrq()?ExecutionExitReason::Waiting:ExecutionExitReason::Budget));
        return {reason,1,ticks,std::nullopt};
    }
    static void* stepWindow(Impl* self,NativeRun* run) {
        auto* current=static_cast<CacheEntry*>(run->current);
        const size_t lookupLimit=std::min<uint32_t>(32,run->limit-run->retired);
        CacheEntry* next=nullptr;
        // Supported native instructions preserve mode, privilege and CP15.
        // A device access, fault or active-code write returns a status.
        // No compilation/eviction occurs inside this window. The lifetime
        // guard must precede every dereference of a borrowed successor.
        const uint32_t targetPc=self->architecturalState.r[15];
        const uint64_t mapping=self->bus.getMappingGeneration(targetPc);
        const uint64_t epoch=self->bus.getCodeEpoch();
        for(auto& slot : current->successors) {
            if(slot.lifetime==self->cacheLifetime) {
                auto* candidate=slot.entry;
                if(candidate && candidate->context[0]==targetPc &&
                    candidate->context[5]<=lookupLimit && lookupLimit<=slot.limit &&
                    candidate->mapping==mapping && candidate->epoch==epoch &&
                    candidate->version==self->bus.getCodePageVersion(candidate->physical)) {
                    next=candidate;
                    next->used=++self->useCounter;
                    ++self->counters.cacheHits; ++self->counters.fastCacheHits; ++self->counters.fastWindowLinks;
                    break;
                }
            }
        }
        if(!next) {
            size_t resolvedLimit=lookupLimit;
            next=self->reusable(lookupLimit,&resolvedLimit);
            if(!next || !next->function) return nullptr;
            bool updated=false;
            for(auto& slot : current->successors) {
                if(slot.lifetime==self->cacheLifetime && slot.entry==next) {
                    slot.limit=std::max(slot.limit,resolvedLimit);
                    updated=true;
                    break;
                }
            }
            if(!updated) {
                if(current->successors[0].lifetime==self->cacheLifetime && current->successors[0].entry)
                    current->successors[1]=current->successors[0];
                current->successors[0]={next,self->cacheLifetime,resolvedLimit};
            }
        }
        ++self->counters.windowLinks;
        run->current=next;
        run->base=run->retired;
        run->physical=next->physical;
        return reinterpret_cast<void*>(next->function);
    }
    uint32_t executeWindow(CacheEntry& entry,NativeRun& run,bool linkWindow) {
        run.current=&entry;
        run.base=run.retired;
        run.physical=entry.physical;
        run.linkWindow=linkWindow?1:0;
        return enterBridge(&architecturalState,&run,reinterpret_cast<void*>(entry.function));
    }
    ExecutionExit executeNative(CacheEntry& entry,ExecutionTimeline& timeline,size_t budget,uint64_t remaining,bool linkWindow=false) {
        const auto startTicks=timeline.elapsed.ticks;
        if(timeline.registerClocks) {
            // Integer/branch and existing checked single-memory instructions are emitted here.
            // The entry limit conservatively fits their worst cost before the
            // first event. The scalar path handles the final crossing instruction.
            const uint32_t frequency=cpu.getExecutionClock();
            const auto limit=clockedLimit(timeline,budget,remaining,frequency);
            NativeRun run{&timeline,&architecturalState,entry.physical,uint32_t(limit)};
            run.frequency=frequency;
            ++counters.nativeWindows;
            const auto status=executeWindow(entry,run,linkWindow);
            if(!run.retired || run.retired>limit || !run.cycles || run.tickedCycles>run.cycles)
                throw std::logic_error("Invalid clocked native block exit");
            bus.tick(timeline.clock.advance(run.cycles-run.tickedCycles,frequency));
            timeline.elapsed.ticks=timeline.clock.elapsedTicks();
            if(!(status&1)) cpu.completeNativeInstruction(run.lastCycles);
            if(status) eventWindowDirty=true;
            counters.internalBackedges+=run.loopTrips;
            counters.generatedSteps+=run.retired;
            counters.generatedCycles+=run.cycles;
            counters.memorySteps+=run.memorySteps;counters.inlineLoads+=run.inlineLoads;
            const uint64_t ticks=timeline.elapsed.ticks-startTicks;
            if(status&1) return {ExecutionExitReason::CpuEvent,run.retired,ticks,CpuStepEvent::DataAbort};
            return {ticks>=remaining?ExecutionExitReason::Deadline:
                (run.retired==budget?ExecutionExitReason::Budget:ExecutionExitReason::BlockEnd),run.retired,ticks,std::nullopt};
        }
        // Source decoding remains capped at 32 instructions. Validated loops
        // may repeat up to the real event/budget boundary without a cache trip.
        size_t executionLimit=std::min<size_t>(budget,65536);
        if(bus.needsLevelInterruptRefresh()) executionLimit=1;
        if(remaining<=executionLimit) executionLimit=std::min<uint64_t>(executionLimit,
            legacyStepsUntilTickLimit(remaining,timeline.ratio,timeline.phase));
        NativeRun run{&timeline,&architecturalState,entry.physical,uint32_t(executionLimit)};
        ++counters.nativeWindows;
        const uint32_t status=executeWindow(entry,run,linkWindow);
        if(status) eventWindowDirty=true;
        const size_t count=run.retired;
        counters.internalBackedges+=run.loopTrips;
        if(count>run.tickedSteps) advanceIntegerPrefix(&bus,&timeline,uint32_t(count-run.tickedSteps));
        const uint64_t ticks=timeline.elapsed.ticks-startTicks;
        counters.generatedSteps+=count; counters.memorySteps+=run.memorySteps; counters.inlineLoads+=run.inlineLoads;
        if(status&1) return {ExecutionExitReason::CpuEvent,count,ticks,CpuStepEvent::DataAbort};
        return {ticks>=remaining?ExecutionExitReason::Deadline:
            (count==budget?ExecutionExitReason::Budget:ExecutionExitReason::BlockEnd),count,ticks,std::nullopt};
    }
};
X64ExecutionEngine::X64ExecutionEngine(ARM920TState& state,ARM920T& cpu,Bus& bus,NativeCacheLimits limits) : impl(std::make_unique<Impl>(state,cpu,bus,limits)) {}
X64ExecutionEngine::~X64ExecutionEngine()=default;
const ARM920TState& X64ExecutionEngine::state() const { return impl->cpu.getState(); }
const Bus& X64ExecutionEngine::memory() const { return impl->bus; }
bool X64ExecutionEngine::canHandoff() const { return impl->cpu.canHandoff(); }
void X64ExecutionEngine::enter(bool clocks) { impl->selectCycleMode(clocks); impl->cpu.enterExecution(clocks); }
const NativeExecutionStats& X64ExecutionEngine::stats() const { return impl->counters; }
void X64ExecutionEngine::resetCodeCache() { impl->clearCache(); ++impl->counters.cacheResets; }
void X64ExecutionEngine::step(ExecutionTimeline& timeline) {
    impl->selectCycleMode(timeline.registerClocks);
    impl->eventWindowDirty=true;
    impl->reference.step(timeline);
    ++impl->counters.fallbackSteps;
}
size_t X64ExecutionEngine::run(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) {
    impl->selectCycleMode(timeline.registerClocks);
    if(timeline.registerClocks && !impl->limits.clockedNative) {
        const auto steps=impl->reference.run(timeline,budget,stopTicks);
        impl->counters.fallbackSteps+=steps;return steps;
    }
    if(timeline.registerClocks) {
        const uint64_t deadline=timeline.elapsed.ticks+std::min(impl->bus.ticksUntilEvent(),
            stopTicks>timeline.elapsed.ticks?stopTicks-timeline.elapsed.ticks:0);
        size_t steps=0;
        while(steps<budget && !state().halted && timeline.elapsed.ticks<deadline) {
            const auto eventDeadline=timeline.elapsed.ticks+std::min(impl->bus.ticksUntilEvent(),deadline-timeline.elapsed.ticks);
            const auto exit=runUntilDeadline(timeline,budget-steps,deadline,eventDeadline,impl->limits.linkNativeWindows);
            steps+=exit.steps;
            if(!exit.steps || state().halted || state().waitingForInterrupt) break;
        }
        return steps;
    }
    size_t steps=0;
    uint64_t eventDeadline=timeline.elapsed.ticks;
    impl->eventWindowDirty=true;
    while(steps<budget && !state().halted && !state().waitingForInterrupt && timeline.elapsed.ticks<stopTicks) {
        if(impl->eventWindowDirty || timeline.elapsed.ticks>=eventDeadline) {
            eventDeadline=timeline.elapsed.ticks+std::min(impl->bus.ticksUntilEvent(),stopTicks-timeline.elapsed.ticks);
            impl->eventWindowDirty=false;
        }
        auto& engine=*impl;
        const auto& current=engine.architecturalState;
        const size_t left=budget-steps;
        if(!engine.bus.wadeTitleBlitEnabled() && engine.cpu.canHandoff() &&
            !(engine.bus.hasPendingIrq() && !(current.cpsr&FLAG_I))) {
            const uint64_t remaining=eventDeadline-timeline.elapsed.ticks;
            size_t count=std::min<size_t>(left,32);
            if(engine.bus.needsLevelInterruptRefresh()) count=1;
            if(remaining<=count) count=std::min<uint64_t>(count,
                legacyStepsUntilTickLimit(remaining,timeline.ratio,timeline.phase));
            auto& slot=engine.hot[Impl::hotIndex(current.r[15])];
            auto* entry=slot.entry;
            if(entry && count<=slot.limit && (entry->function || !entry->nativeEligible)) {
                const auto& key=entry->context;
                if(key[0]==current.r[15] && key[1]==((current.cpsr&0x3fu)|(engine.bus.isUserMode()?0x100u:0u)) &&
                    key[2]==current.cp15_control && key[3]==current.cp15_ttb && key[4]==current.cp15_dacr && key[5]<=count &&
                    entry->mapping==engine.bus.getMappingGeneration(current.r[15]) && entry->epoch==engine.bus.getCodeEpoch() &&
                    entry->version==engine.bus.getCodePageVersion(entry->physical)) {
                    entry->used=++engine.useCounter;
                    ++engine.counters.directCacheHits;
                    ExecutionExit exit;
                    if(entry->function) {
                        ++engine.counters.cacheHits; ++engine.counters.fastCacheHits;
                        exit=engine.executeNative(*entry,timeline,left,remaining,engine.limits.linkNativeWindows);
                    } else {
                        ++engine.counters.fallbackCacheHits;
                        exit=engine.checkedScalarExit(timeline,eventDeadline);
                    }
                    steps+=exit.steps;
                    if(!exit.steps) break;
                    continue;
                }
            }
        }
        if(engine.limits.sampledCompilation && !engine.sampledHotEntry()) {
            // Sampling is a compilation hint only. The existing interpreter
            // owns every cold instruction, interrupt and peripheral step.
            // Its bulk runner avoids decoding/looking up each cold JIT block.
            engine.eventWindowDirty=true;
            const size_t completed=engine.reference.run(timeline,left,stopTicks);
            engine.counters.fallbackSteps+=completed;
            engine.counters.sampledFallbackSteps+=completed;
            ++engine.counters.sampledFallbackBatches;
            steps+=completed;
            if(!completed) break;
            continue;
        }
        const auto exit=runUntilDeadline(timeline,budget-steps,stopTicks,eventDeadline,engine.limits.linkNativeWindows);
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
ExecutionExit X64ExecutionEngine::runUntilDeadline(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks,uint64_t eventDeadline,bool linkWindow) {
    auto& engine=*impl;
    engine.selectCycleMode(timeline.registerClocks);
    const auto& state=engine.cpu.getState();
    // Complex entry conditions retain the scalar contract. The legacy title
    // hook is excluded from native fixtures.
    if (!budget || state.halted || state.waitingForInterrupt ||
        (timeline.registerClocks && !engine.limits.clockedNative) || timeline.elapsed.ticks>=stopTicks || engine.bus.wadeTitleBlitEnabled() ||
        (engine.bus.hasPendingIrq() && !(state.cpsr&FLAG_I)) || !engine.cpu.canHandoff())
        return engine.fallback(timeline,budget,stopTicks);
    const uint64_t remaining=eventDeadline-timeline.elapsed.ticks;
    size_t count=std::min<size_t>(budget,32);
    if(timeline.registerClocks) {
        if(engine.bus.needsLevelInterruptRefresh()) return engine.fallback(timeline,budget,stopTicks);
        const uint32_t frequency=engine.cpu.getExecutionClock();
        if(!frequency) return engine.fallback(timeline,budget,stopTicks);
        count=engine.clockedLimit(timeline,count,remaining,frequency);
        if(!count) return engine.fallback(timeline,budget,stopTicks);
    } else {
        if (engine.bus.needsLevelInterruptRefresh()) count=1;
        if (remaining<=count) count=std::min<uint64_t>(count,legacyStepsUntilTickLimit(remaining,timeline.ratio,timeline.phase));
    }
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
            auto block=decodeBlock(state.r[15],(state.cpsr&FLAG_T)?InstructionSet::Thumb:InstructionSet::Arm,fetch,
                {uint32_t(count),128,engine.limits.followDirectBranches});
            engine.restrictClockedBlock(block);
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
    if(!entry->function) return engine.checkedScalarExit(timeline,eventDeadline);
    return engine.executeNative(*entry,timeline,budget,remaining,linkWindow);
}
} // namespace oceanblast
