#pragma once
#include "timing_model.h"
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

class HostPacingSchedule {
public:
    using Clock = HostPacer::Clock;
    static constexpr auto maxCatchUp = std::chrono::milliseconds(16);

    void reset(Clock::time_point now, uint64_t steps, uint64_t ticks) {
        origin = now;
        originSteps = steps;
        originTicks = ticks;
    }

    Clock::time_point deadline(TimingModel model, uint64_t steps, uint64_t ticks,
                               double capMips, Clock::time_point now) {
        const auto offset = std::chrono::duration<double>(
            hostPacingSeconds(model, steps - originSteps, ticks - originTicks, capMips));
        const auto target = origin + std::chrono::duration_cast<Clock::duration>(offset);
        const auto earliest = now - maxCatchUp;
        if (target >= earliest) return target;

        // A long host stall must not build up a burst of guest work for later.
        discardedLag += earliest - target;
        reset(earliest, steps, ticks);
        return earliest;
    }

    double discardedLagMilliseconds() const {
        return std::chrono::duration<double, std::milli>(discardedLag).count();
    }

private:
    Clock::time_point origin{};
    uint64_t originSteps = 0, originTicks = 0;
    Clock::duration discardedLag{};
};
}
