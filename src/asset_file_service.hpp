#pragma once
#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include <future>
namespace forge {
enum class AssetFileState { Idle, Preparing, Review, Committing, Complete, Failed, Cancelled };
struct AssetFileReview {
    AssetFilePlan plan;
    AssetReferenceImpact impact;
    nlohmann::json schema;
    std::vector<AssetReferenceDocument> drafts;
};
// One job per owning application thread. Caller drains other project writers first
// and excludes Save/import/project-switch until busy() becomes false.
class AssetFileService {
  public:
    explicit AssetFileService(std::shared_ptr<const ProjectLease>);
    ~AssetFileService();
    void prepare(AssetRecord source, AssetFileRequest, nlohmann::json schema,
                 std::vector<AssetReferenceDocument> drafts = {});
    void commit();
    void cancel();
    void poll();
    bool busy() const;
    AssetFileState state() const { return state_; }
    std::shared_ptr<const AssetFileReview> review() const { return review_; }
    const std::string& diagnostic() const { return diagnostic_; }
    const std::optional<AssetFileCommit>& receipt() const { return receipt_; }
    std::shared_ptr<const AssetCatalog> catalog() const { return catalog_; }

  private:
    struct Result {
        std::shared_ptr<const AssetFileReview> review;
        std::optional<AssetFileCommit> receipt;
        std::shared_ptr<const AssetCatalog> catalog;
    };
    std::shared_ptr<const ProjectLease> lease_;
    const std::thread::id owner_ = std::this_thread::get_id();
    AssetFileState state_ = AssetFileState::Idle;
    std::stop_source stop_;
    std::shared_ptr<const AssetFileReview> review_;
    std::optional<AssetFileCommit> receipt_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::string diagnostic_;
    // Future joins before captured state/lease is destroyed.
    std::future<Result> job_;
    void check() const;
};
} // namespace forge
