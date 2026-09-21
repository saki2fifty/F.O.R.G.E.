#pragma once
#include <forge/asset_discovery.hpp>
#include <future>
#include <optional>
#include <thread>
namespace forge {
struct AssetWatchUpdate {
    std::shared_ptr<const SourceSnapshot> snapshot;
    std::vector<SourceChange> changes;
    std::uint64_t generation = 0;
};
// Portable polling adapter for the existing source tracker. All file hashing is
// asynchronous; delivery, self-write acknowledgements and lifecycle are owner-only.
// This is not an OS event watcher or a second source/asset identity registry.
class AssetSourceWatch {
  public:
    using Clock = SourceChangeTracker::Clock;
    explicit AssetSourceWatch(std::filesystem::path project, SourceScanOptions options = {},
                              std::chrono::milliseconds interval = std::chrono::seconds(2),
                              std::chrono::milliseconds debounce = std::chrono::milliseconds(200));
    ~AssetSourceWatch();
    void rescan();
    void acknowledge_write(const std::filesystem::path&, std::string digest);
    std::optional<AssetWatchUpdate> poll(Clock::time_point now = Clock::now());
    bool scanning() const;
    std::uint64_t generation() const;
    bool complete() const;

  private:
    void check() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::filesystem::path project_;
    SourceScanOptions options_;
    std::chrono::milliseconds interval_;
    SourceChangeTracker tracker_;
    std::stop_source stop_;
    std::future<SourceSnapshot> scan_;
    Clock::time_point next_{};
    bool requested_ = true;
    std::shared_ptr<const SourceSnapshot> latest_;
};
} // namespace forge
