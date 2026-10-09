// Sample the oldest thread of a local emulator process; never read guest RAM.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: sample_native PID SECONDS OUTPUT.csv\n"; return 1;
    }
    DWORD pid; unsigned seconds;
    try { pid = std::stoul(argv[1]); seconds = std::stoul(argv[2]); }
    catch (...) { return 1; }
    if (!pid || !seconds || seconds > 3600 || sizeof(void*) != 8) return 1;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) { std::cerr << "Cannot open process: " << GetLastError() << '\n'; return 1; }
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32 module{}; module.dwSize = sizeof(module);
    if (modules == INVALID_HANDLE_VALUE || !Module32First(modules, &module)) {
        if (modules != INVALID_HANDLE_VALUE) CloseHandle(modules);
        CloseHandle(process); return 1;
    }
    const uint64_t base = reinterpret_cast<uint64_t>(module.modBaseAddr), size = module.modBaseSize;
    CloseHandle(modules);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    DWORD threadId = 0; uint64_t oldest = UINT64_MAX;
    if (snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) continue;
            HANDLE candidate = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
            FILETIME created{}, exited{}, kernel{}, user{};
            if (candidate && GetThreadTimes(candidate, &created, &exited, &kernel, &user)) {
                const uint64_t time = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
                if (time < oldest) { oldest = time; threadId = entry.th32ThreadID; }
            }
            if (candidate) CloseHandle(candidate);
        } while (Thread32Next(snapshot, &entry));
    }
    if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (!thread) { CloseHandle(process); return 1; }
    std::map<uint64_t, uint64_t> counts;
    uint64_t outside = 0, failures = 0, total = 0;
    const uint64_t end = GetTickCount64() + seconds * 1000ull;
    while (GetTickCount64() < end && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        Sleep(5);
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) break;
        if (SuspendThread(thread) == DWORD(-1)) { ++failures; break; }
        CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
        const bool captured = GetThreadContext(thread, &context) != 0;
        // Resume unconditionally, including when context capture fails.
        if (ResumeThread(thread) == DWORD(-1)) { ++failures; break; }
        if (!captured) { ++failures; continue; }
        ++total;
        if (context.Rip >= base && context.Rip - base < size) ++counts[context.Rip - base];
        else ++outside;
    }
    CloseHandle(thread); CloseHandle(process);
    std::ofstream output(argv[3]);
    output << "rva,samples\n";
    for (const auto& point : counts) output << "0x" << std::hex << point.first << std::dec << ',' << point.second << '\n';
    std::cout << "Samples: " << total << "; outside executable: " << outside
              << "; capture failures: " << failures << '\n';
    return !output || !total || failures ? 1 : 0;
}
