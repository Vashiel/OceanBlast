#pragma once
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace oceanblast {
class HostPacer {
public:
    using Clock = std::chrono::steady_clock;
    explicit HostPacer(bool enabled = true) {
#ifdef _WIN32
        if (enabled) {
            timer = CreateWaitableTimerExW(nullptr, nullptr,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
            highResolution_ = timer != nullptr;
            if (!timer) {
                timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
                periodRequested_ = timeBeginPeriod(1) == TIMERR_NOERROR;
            }
        }
#else
        (void)enabled;
#endif
    }
    ~HostPacer() {
#ifdef _WIN32
        if (timer) CloseHandle(timer);
        if (periodRequested_) timeEndPeriod(1);
#endif
    }
    HostPacer(const HostPacer&) = delete;
    HostPacer& operator=(const HostPacer&) = delete;
    bool highResolution() const {
#ifdef _WIN32
        return highResolution_;
#else
        return false;
#endif
    }
    void waitUntil(Clock::time_point deadline) {
        const auto now = Clock::now();
        if (deadline <= now) return;
#ifdef _WIN32
        if (timer) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
            LARGE_INTEGER due{}; due.QuadPart = -((ns + 99) / 100);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) &&
                WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0) return;
        }
#endif
        std::this_thread::sleep_until(deadline);
    }
private:
#ifdef _WIN32
    HANDLE timer = nullptr;
    bool highResolution_ = false, periodRequested_ = false;
#endif
};
}
