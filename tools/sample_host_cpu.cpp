// Windows user-mode instruction-pointer sampler for an already running emulator.
// Usage: sample_host_cpu.exe <pid> <duration_ms> [interval_ms]
#ifdef _WIN64
#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>

static uint64_t filetimeValue(FILETIME value) {
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: sample_host_cpu.exe <pid> <duration_ms> [interval_ms]\n";
        return 1;
    }
    const DWORD pid = static_cast<DWORD>(std::stoul(argv[1]));
    const unsigned durationMs = static_cast<unsigned>(std::stoul(argv[2]));
    const unsigned intervalMs = argc == 4 ? static_cast<unsigned>(std::stoul(argv[3])) : 2;
    if (!pid || !durationMs || !intervalMs) return 1;

    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (modules == INVALID_HANDLE_VALUE || !Module32FirstW(modules, &module)) {
        std::cerr << "Cannot inspect target executable module.\n";
        if (modules != INVALID_HANDLE_VALUE) CloseHandle(modules);
        return 1;
    }
    const uint64_t base = reinterpret_cast<uint64_t>(module.modBaseAddr);
    const uint64_t end = base + module.modBaseSize;
    CloseHandle(modules);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD threadId = 0;
    uint64_t mostCpuTime = 0;
    if (snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) continue;
            HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            FILETIME created{}, exited{}, kernel{}, user{};
            const bool valid = GetThreadTimes(thread, &created, &exited, &kernel, &user);
            const uint64_t cpuTime = valid ? filetimeValue(kernel) + filetimeValue(user) : 0;
            if (!threadId || cpuTime > mostCpuTime) {
                threadId = entry.th32ThreadID;
                mostCpuTime = cpuTime;
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    HANDLE thread = threadId ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId) : nullptr;
    if (!thread) {
        std::cerr << "Cannot open target CPU thread.\n";
        return 1;
    }

    std::map<uint64_t, uint64_t> offsets;
    uint64_t outsideModule = 0, failed = 0;
    const bool raisedTimerResolution = timeBeginPeriod(1) == TIMERR_NOERROR;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        Sleep(intervalMs);
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) { ++failed; continue; }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        const bool valid = GetThreadContext(thread, &context);
        ResumeThread(thread);
        if (!valid) { ++failed; continue; }
        const uint64_t pc = context.Rip;
        if (pc >= base && pc < end) ++offsets[pc - base];
        else ++outsideModule;
    }
    if (raisedTimerResolution) timeEndPeriod(1);
    CloseHandle(thread);
    std::cerr << "pid=" << pid << " thread=" << threadId << " runtime_base=0x" << std::hex << base
              << " module_bytes=0x" << module.modBaseSize << std::dec
              << " outside_module=" << outsideModule << " failed=" << failed << '\n';
    std::cout << "module_offset,samples\n";
    for (const auto& [offset, count] : offsets)
        std::cout << "0x" << std::hex << offset << std::dec << ',' << count << '\n';
    return 0;
}
#else
#include <iostream>
int main() {
    std::cerr << "This sampler requires a 64-bit Windows build.\n";
    return 1;
}
#endif
