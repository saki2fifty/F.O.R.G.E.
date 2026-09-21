#include "asset_watch.hpp"
namespace forge {
AssetSourceWatch::AssetSourceWatch(std::filesystem::path project, SourceScanOptions options,
                                   std::chrono::milliseconds interval,
                                   std::chrono::milliseconds debounce)
    : project_(ProjectPaths(project).root()), options_(std::move(options)), interval_(interval),
      tracker_({}, debounce) {
    if (interval < std::chrono::milliseconds(100) || interval > std::chrono::minutes(10))
        throw std::runtime_error("Source watch interval must be between 100ms and ten minutes");
}
AssetSourceWatch::~AssetSourceWatch() {
    stop_.request_stop(); // future joins before the remaining owners disappear.
}
void AssetSourceWatch::check() const {
    if (std::this_thread::get_id() != owner_)
        throw std::runtime_error("Source watch operation requires its owning thread");
}
void AssetSourceWatch::rescan() {
    check();
    requested_ = true;
}
void AssetSourceWatch::acknowledge_write(const std::filesystem::path& path, std::string digest) {
    check();
    tracker_.acknowledge_write(path, std::move(digest));
}
bool AssetSourceWatch::scanning() const {
    check();
    return scan_.valid();
}
std::uint64_t AssetSourceWatch::generation() const {
    check();
    return tracker_.generation();
}
bool AssetSourceWatch::complete() const {
    check();
    return latest_ && tracker_.complete();
}
std::optional<AssetWatchUpdate> AssetSourceWatch::poll(Clock::time_point now) {
    check();
    bool observed = false;
    if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        SourceSnapshot snapshot;
        try {
            snapshot = scan_.get();
        } catch (const std::exception& e) {
            snapshot.complete = false;
            snapshot.diagnostics.push_back({{}, "watch_scan_failed", e.what(), true});
        }
        latest_ = std::make_shared<const SourceSnapshot>(snapshot);
        tracker_.observe(std::move(snapshot), now);
        next_ = now + interval_;
        observed = true;
    }
    auto changes = tracker_.drain(now);
    if (!scan_.valid() && (requested_ || now >= next_)) {
        requested_ = false;
        const auto project = project_;
        const auto options = options_;
        scan_ = std::async(std::launch::async, [project, options, stop = stop_.get_token()] {
            return scan_asset_sources(project, options, stop);
        });
    }
    if (!observed && changes.empty())
        return {};
    return AssetWatchUpdate{latest_, std::move(changes), tracker_.generation()};
}
} // namespace forge
