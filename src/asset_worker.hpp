#pragma once
#include <cstdint>
#include <filesystem>
#include <stop_token>
namespace forge::asset_detail {
class WorkerStageLease;
enum class WorkerKind { Animation, Navigation, Script, Import, Schema };
struct WorkerLimits {
    std::uint64_t memory_bytes = 512ull * 1024 * 1024;
    std::uint64_t file_bytes = 16ull * 1024 * 1024;
    std::uint64_t total_bytes = 32ull * 1024 * 1024;
    unsigned seconds = 30, cpu_seconds = 25, files = 4096;
    // Import workers use a fixed cancel.request marker and flat output/ directory.
    // Zero retains immediate cancellation for the existing converter workers.
    unsigned cancellation_grace_ms = 0;
};
// Fixed commands only. Explicit executable/cwd, cancellation, bounded child lifetime.
void run_worker(WorkerKind, const std::filesystem::path& executable,
                const std::filesystem::path& staging, std::stop_token cancel,
                WorkerLimits limits = {}, const WorkerStageLease* staging_owner = nullptr);
} // namespace forge::asset_detail
