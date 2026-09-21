#include "asset_file_service.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include <forge/prefab.hpp>
#include <forge/scene.hpp>
namespace forge {
namespace {
// Source-owned authored IDs may have been discovered without a saved catalog entry.
// Registration is an explicit non-destructive precursor, independent of file confirmation.
void index_authored_source(const ProjectLease& lease, const AssetRecord& record) {
    const ProjectPaths paths(lease.root());
    const auto index_path = AssetCatalog::project_index(lease.root());
    const auto before = asset_storage::read(index_path);
    auto catalog = AssetCatalog::open_project(lease.root());
    if (const auto found = catalog.records().find(record.id); found != catalog.records().end()) {
        if (found->second.type != record.type || found->second.source != record.source)
            throw std::runtime_error("Asset identity/locator changed before operation preparation");
        return;
    }
    if (record.type != "scene" && record.type != "prefab")
        throw std::runtime_error("Import/register this source before using file operations");
    const auto source = asset_storage::read(paths.resolve(record.source));
    if (!source)
        throw std::runtime_error("Authored source is missing");
    const auto doc =
        asset_detail::parse_bounded_json(std::as_bytes(std::span(*source)), max_asset_index_bytes);
    if (doc.at("asset_id").get<AssetId>() != record.id)
        throw std::runtime_error("Authored source identity changed");
    if (record.type == "scene")
        Scene::validate_document(doc);
    else
        PrefabDocument::validate(doc);
    catalog.add({record.id, record.type, record.source, doc.at("version").get<unsigned>(), {}});
    const auto candidate = catalog.document().dump(2);
    lease.check();
    if (asset_storage::read(index_path) != before ||
        asset_storage::read(paths.resolve(record.source)) != source)
        throw std::runtime_error("Source/catalog changed before identity registration");
    asset_storage::replace(index_path, candidate);
}
} // namespace
AssetFileService::AssetFileService(std::shared_ptr<const ProjectLease> lease)
    : lease_(std::move(lease)) {
    if (!lease_)
        throw std::runtime_error("Asset file jobs require project writer ownership");
    check();
}
AssetFileService::~AssetFileService() { stop_.request_stop(); }
void AssetFileService::check() const {
    if (std::this_thread::get_id() != owner_)
        throw std::runtime_error("Asset file jobs require their owning application thread");
    lease_->check();
}
bool AssetFileService::busy() const {
    return state_ == AssetFileState::Preparing || state_ == AssetFileState::Review ||
           state_ == AssetFileState::Committing;
}
void AssetFileService::prepare(AssetRecord source, AssetFileRequest request, nlohmann::json schema,
                               std::vector<AssetReferenceDocument> drafts) {
    check();
    if (busy() || job_.valid())
        throw std::runtime_error("Finish the current asset file operation first");
    if (source.id != request.asset || source.subasset)
        throw std::runtime_error("File operation requires its source owner asset");
    stop_ = std::stop_source{};
    review_.reset();
    receipt_.reset();
    catalog_.reset();
    diagnostic_.clear();
    job_ = std::async(std::launch::async, [lease = lease_, source = std::move(source), request,
                                           schema = std::move(schema), drafts = std::move(drafts),
                                           stop = stop_.get_token()]() mutable {
        AssetFileTransaction transaction(*lease);
        transaction.recover();
        if (stop.stop_requested())
            throw std::runtime_error("Asset file operation cancelled");
        index_authored_source(*lease, source);
        auto review = std::make_shared<AssetFileReview>();
        review->plan = prepare_asset_file_operation(
            lease->root(), request, project_asset_file_rewriter(lease->root()), stop);
        const std::set<AssetId> targets(review->plan.affected.begin(), review->plan.affected.end());
        review->impact = scan_asset_references(lease->root(), schema, targets, drafts, stop);
        review->schema = std::move(schema);
        review->drafts = std::move(drafts);
        return Result{
            std::move(review),
            {},
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(lease->root()))};
    });
    state_ = AssetFileState::Preparing;
}
void AssetFileService::commit() {
    check();
    if (state_ != AssetFileState::Review || !review_ || job_.valid())
        throw std::runtime_error("Prepare and review the file operation before committing");
    job_ = std::async(std::launch::async, [lease = lease_, review = review_,
                                           stop = stop_.get_token()] {
        const std::set<AssetId> targets(review->plan.affected.begin(), review->plan.affected.end());
        const auto current =
            scan_asset_references(lease->root(), review->schema, targets, review->drafts, stop);
        if (current.reviewed_sources != review->impact.reviewed_sources)
            throw std::runtime_error(
                "Project reference sources changed since review. Prepare the operation again.");
        // Allocate/validate the adoption receipt before the transaction commit point.
        AssetCatalog candidate(lease->root());
        candidate.restore(nlohmann::json::parse(*review->plan.changes.back().after));
        auto catalog = std::make_shared<const AssetCatalog>(std::move(candidate));
        AssetFileTransaction transaction(*lease);
        auto receipt = transaction.commit(
            review->plan.changes, review->plan.request.action == AssetFileAction::Delete, stop);
        return Result{review, std::move(receipt), std::move(catalog)};
    });
    state_ = AssetFileState::Committing;
}
void AssetFileService::cancel() {
    check();
    stop_.request_stop();
    if (state_ == AssetFileState::Review)
        state_ = AssetFileState::Cancelled;
}
void AssetFileService::poll() {
    check();
    if (!job_.valid() || job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try {
        auto result = job_.get();
        review_ = std::move(result.review);
        receipt_ = std::move(result.receipt);
        catalog_ = std::move(result.catalog);
        // Cancellation after commit cannot pretend that the successful operation rolled back.
        state_ = receipt_                 ? AssetFileState::Complete
                 : stop_.stop_requested() ? AssetFileState::Cancelled
                                          : AssetFileState::Review;
    } catch (const std::exception& e) {
        diagnostic_ = e.what();
        state_ = stop_.stop_requested() ? AssetFileState::Cancelled : AssetFileState::Failed;
    }
}
} // namespace forge
