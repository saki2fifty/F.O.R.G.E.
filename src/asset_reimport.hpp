#pragma once
#include "asset_import_service.hpp"
#include "asset_watch.hpp"
#include <set>
namespace forge {
struct AssetReimportRoute {
    std::string importer;
    ImportTarget target;
    std::shared_ptr<const AssetImporterRegistry> registry;
    AssetImportService::PreparePublication prepare;
};
// Owns automatic reimport of already registered assets. New source discovery is
// exposed separately; identity allocation/ambiguous importer choice stays explicit.
class AssetReimportService {
  public:
    AssetReimportService(std::shared_ptr<const ProjectLease>, std::vector<AssetReimportRoute>,
                         SourceScanOptions = {},
                         std::chrono::milliseconds interval = std::chrono::seconds(2),
                         std::chrono::milliseconds debounce = std::chrono::milliseconds(200));
    std::function<bool(AssetId)> blocked;
    void rescan(bool retry_failed = true);
    void catalog_changed(std::shared_ptr<const AssetCatalog>);
    std::vector<AssetImportOutcome> poll();
    std::shared_ptr<const AssetCatalog> catalog() const { return catalog_; }
    std::shared_ptr<const SourceSnapshot> sources() const { return sources_; }
    std::vector<AssetJobInfo> jobs() const;
    std::size_t queued() const { return queue_.size(); }
    bool scanning() const { return watch_.scanning(); }
    bool complete() const { return watch_.complete(); }
    std::uint64_t generation() const { return watch_.generation(); }

  private:
    struct Route {
        AssetReimportRoute profile;
        std::unique_ptr<AssetImportService> service;
    };
    struct Active {
        AssetId asset;
        std::size_t route = 0;
        AssetJobId job = 0;
        bool superseded = false;
    };
    std::shared_ptr<const ProjectLease> lease_;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Route> routes_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::shared_ptr<const SourceSnapshot> sources_;
    AssetSourceWatch watch_;
    std::set<AssetId> queue_, failed_;
    std::optional<Active> active_;
    std::uint64_t observed_ = 0;
    AssetId cursor_;
    void check() const;
    std::optional<std::size_t> route(const AssetRecord&) const;
    AssetId root(AssetId) const;
    void enqueue(AssetId);
    void dependents(AssetId);
    bool source_current(const AssetRecord&) const;
    void observe(const AssetWatchUpdate&);
    void cancel_active();
};
} // namespace forge
