#pragma once
#include <forge/asset_jobs.hpp>
#include <forge/asset_publication.hpp>

namespace forge {
// Borrowed exact-source authoring API, not a stable binary plugin contract.
struct AssetImportDraft {
    AssetImportRequest request;
    AssetPublicationTicket ticket;
    std::shared_ptr<const AssetImporter> importer;
};
struct AssetImportOutcome {
    AssetJobInfo job;
    bool published = false;
    bool cache_hit = false;
    std::string diagnostic;
    std::optional<AssetPublicationResult> publication;
    // Structured owner-thread reconciliation failure; no assets were published.
    std::vector<SubassetIdentityConflict> identity_conflicts;
};
class AssetImportService {
  public:
    // Runs on the owner after worker completion. Supplies this importer's complete
    // root/member mapping without changing files, worlds or live resources.
    using PreparePublication =
        std::function<void(AssetPublicationCandidate&, const AssetImportPlan&)>;
    AssetImportService(std::shared_ptr<const ProjectLease> lease,
                       std::shared_ptr<const AssetImporterRegistry> registry, ImportTarget target,
                       unsigned workers = 2);
    ~AssetImportService();
    AssetImportService(const AssetImportService&) = delete;
    AssetImportService& operator=(const AssetImportService&) = delete;
    // Reads bounded metadata/prefix only; source hashing and native work run in queue.
    AssetImportDraft prepare(const std::filesystem::path& source,
                             std::optional<std::string_view> importer = {});
    AssetJobId submit(AssetImportDraft draft, PreparePublication prepare,
                      AssetPublisher::Compatibility compatibility, int priority = 0);
    void cancel(AssetJobId job);
    std::vector<AssetJobInfo> jobs() const;
    // Publication is explicit owner-thread work and is never scene Undo.
    std::vector<AssetImportOutcome> poll();
    bool wait_idle(std::chrono::milliseconds timeout);

  private:
    struct Pending;
    std::shared_ptr<const ProjectLease> lease_;
    std::shared_ptr<const AssetImporterRegistry> registry_;
    ImportTarget target_;
    AssetPublisher publisher_;
    std::thread::id owner_;
    std::uint64_t next_generation_ = 1;
    std::map<AssetJobId, std::shared_ptr<Pending>> pending_;
    std::map<AssetJobId, AssetJobInfo> receipts_;
    // Destroy/join before any pending callbacks or lease owners disappear.
    AssetBuildQueue queue_;
    void check() const;
};
} // namespace forge
