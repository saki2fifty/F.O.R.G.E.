#pragma once
#include <chrono>
#include <cstdint>
#include <forge/project_paths.hpp>
#include <map>
#include <stop_token>
#include <vector>

namespace forge {
struct SourceFile {
    std::filesystem::path source;
    std::string digest;
    std::uint64_t bytes = 0;
    // OS file identity is session evidence only. Never serialize it as AssetId.
    std::string file_identity;
    std::string source_kind;
    std::filesystem::path alias_of;
};
struct SourceScanDiagnostic {
    std::filesystem::path source;
    std::string code;
    std::string message;
    bool error = false;
};
struct SourceSnapshot {
    // Match the project locator policy: Windows ordinal case-insensitive; POSIX exact.
    std::map<std::filesystem::path, SourceFile, ProjectLocatorLess> files;
    std::vector<SourceScanDiagnostic> diagnostics;
    std::uint64_t bytes_read = 0;
    std::size_t filtered = 0;
    bool complete = true;
};
struct SourceScanOptions {
    std::vector<std::filesystem::path> roots{"Assets"};
    // Explicit project-relative directory/file prefixes; no platform-dependent globs.
    std::vector<std::filesystem::path> ignored;
    std::size_t max_files = 100000, max_directories = 10000, max_depth = 64;
    std::uint64_t max_file_bytes = 512ULL * 1024 * 1024;
    std::uint64_t max_total_bytes = 8ULL * 1024 * 1024 * 1024;
    // Scan project-contained sources outside Assets as well. The project root is
    // a traversal scope only; it never becomes a valid asset/source locator.
    bool include_project_root = false;
};
// Read-only, deterministic full rescan. All file contents are hashed; timestamps
// alone never establish that a source is unchanged. Safe to call on a worker.
SourceSnapshot scan_asset_sources(const std::filesystem::path& project,
                                  const SourceScanOptions& options = {}, std::stop_token stop = {});
enum class SourceChangeKind { Created, Modified, Removed, Moved };
struct SourceChange {
    SourceChangeKind kind;
    std::filesystem::path source, previous_source;
    std::string digest, previous_digest;
    std::uint64_t generation = 0;
};
// Owner-thread state for polling/manual-rescan or native watcher adapters.
// Generations advance at observation, before debounce/publication. Incomplete
// scans block delivery and never infer deletions. This class does not edit catalog IDs.
class SourceChangeTracker {
  public:
    using Clock = std::chrono::steady_clock;
    explicit SourceChangeTracker(SourceSnapshot baseline, std::chrono::milliseconds debounce =
                                                              std::chrono::milliseconds(200));
    void observe(SourceSnapshot snapshot, Clock::time_point now);
    std::vector<SourceChange> drain(Clock::time_point now);
    void acknowledge_write(const std::filesystem::path& source, std::string expected_digest);
    std::uint64_t generation() const { return generation_; }
    bool complete() const { return complete_; }
    const SourceSnapshot& latest() const { return latest_; }

  private:
    SourceSnapshot baseline_, latest_;
    std::chrono::milliseconds debounce_;
    Clock::time_point changed_{};
    std::map<std::filesystem::path, std::string, ProjectLocatorLess> writes_;
    std::uint64_t generation_ = 1;
    bool pending_ = false, complete_ = true;
};
} // namespace forge
