#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// PSAPI declarations require Windows types to be defined first.
#include <psapi.h>
#endif
namespace forge {
struct ProcessCounters {
    std::optional<std::uint64_t> cpu_ticks, working_set, private_bytes;
    unsigned processors = 0;
};
inline ProcessCounters process_counters() {
    ProcessCounters result;
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        auto ticks = [](FILETIME t) {
            return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        };
        result.cpu_ticks = ticks(kernel) + ticks(user);
    }
    result.processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(),
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                sizeof(memory))) {
        result.working_set = memory.WorkingSetSize;
        result.private_bytes = memory.PrivateUsage;
    }
#endif
    return result;
}
inline std::optional<double> cpu_percent(const ProcessCounters& before,
                                         const ProcessCounters& after, double seconds) {
    if (!before.cpu_ticks || !after.cpu_ticks || !after.processors || !std::isfinite(seconds) ||
        seconds <= 0 || *after.cpu_ticks < *before.cpu_ticks)
        return std::nullopt;
    return std::clamp(double(*after.cpu_ticks - *before.cpu_ticks) / 10000000.0 / seconds /
                          after.processors * 100.0,
                      0.0, 100.0);
}
class FrameAverages {
  public:
    double fps = 0, milliseconds = 0;
    bool add(double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0)
            return false;
        elapsed_ += seconds;
        ++frames_;
        if (elapsed_ < 0.5)
            return false;
        fps = frames_ / elapsed_;
        milliseconds = 1000 * elapsed_ / frames_;
        elapsed_ = 0;
        frames_ = 0;
        return true;
    }

  private:
    double elapsed_ = 0;
    unsigned frames_ = 0;
};
class Telemetry {
    using Clock = std::chrono::steady_clock;

  public:
    FrameAverages frames;
    ProcessCounters process = process_counters();
    std::optional<double> cpu;
    void frame() {
        const auto now = Clock::now();
        const double delta = std::chrono::duration<double>(now - last_frame_).count();
        last_frame_ = now;
        if (frames.add(delta)) {
            const auto next = process_counters();
            cpu = cpu_percent(process, next,
                              std::chrono::duration<double>(now - last_sample_).count());
            process = next;
            last_sample_ = now;
        }
    }
    void pause() {
        last_frame_ = last_sample_ = Clock::now();
        frames = {};
        cpu.reset();
        process = process_counters();
    }

  private:
    Clock::time_point last_frame_ = Clock::now(), last_sample_ = last_frame_;
};
} // namespace forge
