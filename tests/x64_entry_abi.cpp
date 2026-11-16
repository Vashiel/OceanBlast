#include "cpu/arm920t_state.h"
#include <asmjit/x86.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
using namespace oceanblast;
struct Frame {
    void* targets[2]{};
    void* exit=nullptr;
    uintptr_t stack=0;
    uint64_t retired=0,limit=0,helpers=0;
};
static void helper(ARM920TState* state,Frame* frame) {
    volatile uint64_t scratch[8]{};
    scratch[frame->retired&7]=frame->retired;
    state->r[1]+=uint32_t(scratch[frame->retired&7]!=0)*3;
    ++frame->helpers;
}
int main() {
    using namespace asmjit;
    JitAllocator::CreateParams params{};
    params.options=JitAllocatorOptions::kUseDualMapping;
    JitRuntime runtime(&params);
    CodeHolder bridge;
    if(bridge.init(runtime.environment())!=Error::kOk)return 1;
    x86::Assembler a(&bridge);
    const auto exit=a.new_label(),done=a.new_label();
    // Outer ABI:
    // - Windows x64: RCX state, RDX frame, R8 first block (32-byte shadow space + alignment -> sub rsp, 40).
    // - System-V AMD64: RDI state, RSI frame, RDX first block (16-byte alignment -> sub rsp, 8).
    // Internal ABI: R12 state, R13 frame. RSP stays fixed across all edges.
#ifdef _WIN32
    constexpr int32_t kFrameReserve = 40;
    const x86::Gp arg0 = x86::rcx;
    const x86::Gp arg1 = x86::rdx;
    const x86::Gp arg2 = x86::r8;
#else
    constexpr int32_t kFrameReserve = 8;
    const x86::Gp arg0 = x86::rdi;
    const x86::Gp arg1 = x86::rsi;
    const x86::Gp arg2 = x86::rdx;
#endif
    a.push(x86::r12);a.push(x86::r13);a.sub(x86::rsp,kFrameReserve);
    a.mov(x86::r12,arg0);a.mov(x86::r13,arg1);
    a.mov(x86::qword_ptr(x86::r13,offsetof(Frame,stack)),x86::rsp);
    a.lea(x86::rax,x86::ptr(exit));a.mov(x86::qword_ptr(x86::r13,offsetof(Frame,exit)),x86::rax);
    a.cmp(x86::qword_ptr(x86::r13,offsetof(Frame,limit)),0);a.je(done);
    a.jmp(arg2);
    a.bind(done);a.xor_(x86::eax,x86::eax);
    a.bind(exit);a.add(x86::rsp,kFrameReserve);a.pop(x86::r13);a.pop(x86::r12);a.ret();
    using Entry=uint32_t(*)(ARM920TState*,Frame*,void*);
    Entry enter=nullptr;
    if(runtime.add(&enter,&bridge)!=Error::kOk)return 1;
    void* blocks[2]{};
    for(unsigned index=0;index<2;++index) {
        CodeHolder code;if(code.init(runtime.environment())!=Error::kOk)return 1;
        x86::Assembler b(&code);
        const auto bad=b.new_label(),finished=b.new_label();
        b.cmp(x86::rsp,x86::qword_ptr(x86::r13,offsetof(Frame,stack)));b.jne(bad);
        b.mov(x86::rax,x86::rsp);b.and_(x86::eax,15);b.test(x86::eax,x86::eax);b.jnz(bad);
        b.add(x86::dword_ptr(x86::r12,offsetof(ARM920TState,r)),index+1);
        b.inc(x86::qword_ptr(x86::r13,offsetof(Frame,retired)));
        if(index) {
            b.mov(arg0,x86::r12);b.mov(arg1,x86::r13);
            b.mov(x86::rax,uintptr_t(&helper));b.call(x86::rax);
        }
        b.mov(x86::rax,x86::qword_ptr(x86::r13,offsetof(Frame,retired)));
        b.cmp(x86::rax,x86::qword_ptr(x86::r13,offsetof(Frame,limit)));b.jae(finished);
        // Distinct executable allocations; no recursive call or host return.
        b.jmp(x86::qword_ptr(x86::r13,offsetof(Frame,targets)+(1-index)*sizeof(void*)));
        b.bind(finished);b.xor_(x86::eax,x86::eax);b.jmp(x86::qword_ptr(x86::r13,offsetof(Frame,exit)));
        b.bind(bad);b.mov(x86::eax,1);b.jmp(x86::qword_ptr(x86::r13,offsetof(Frame,exit)));
        if(runtime.add(&blocks[index],&code)!=Error::kOk)return 1;
    }
    bool ok=true;
    for(uint64_t budget:{0ull,1ull,2ull,3ull,65ull,65536ull}) {
        ARM920TState state{};Frame frame;
        frame.targets[0]=blocks[0];frame.targets[1]=blocks[1];frame.limit=budget;
        const auto status=enter(&state,&frame,blocks[0]);
        ok=ok&&status==0&&frame.retired==budget&&frame.helpers==budget/2&&
            state.r[0]==budget+budget/2&&state.r[1]==3*(budget/2);
    }
    for(auto block:blocks)runtime.release(block);
    runtime.release(enter);
    std::cout<<(ok?"PASS ":"FAIL ")<<"Shared x64 entry preserves fixed stack, direct cross-allocation jumps, helper calls and exact budgets\n";
    return ok?0:1;
}
