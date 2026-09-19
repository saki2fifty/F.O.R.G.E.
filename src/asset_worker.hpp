#pragma once
#include <filesystem>
#include <stop_token>
namespace forge::asset_detail {
enum class WorkerKind { Animation, Navigation, Script };
// Fixed commands only. Explicit executable/cwd, cancellation, bounded child lifetime.
void run_worker(WorkerKind, const std::filesystem::path& executable,
                const std::filesystem::path& staging, std::stop_token cancel);
} // namespace forge::asset_detail
