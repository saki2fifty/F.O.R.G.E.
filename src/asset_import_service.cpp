#include "asset_import_service.hpp"
#include "asset_file_transaction.hpp"
#include "bounded_json.hpp"
#include "import_cache_limits.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
namespace forge {
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
const ProjectLease& required_lease(const std::shared_ptr<const ProjectLease>& lease) {
    require(bool(lease), "Asset imports require project writer ownership");
    lease->check();
    return *lease;
}
AssetCatalog catalog_from(const std::filesystem::path& project,
                          const std::optional<std::string>& bytes) {
    AssetCatalog result(project);
    if (bytes)
        result.restore(asset_detail::parse_bounded_json(std::as_bytes(std::span(*bytes)),
                                                        max_asset_index_bytes));
    return result;
}
nlohmann::json family(const AssetCatalog& catalog, AssetId owner) {
    auto result = nlohmann::json::array();
    const auto document = catalog.document();
    for (const auto& item : document.at("assets")) {
        const auto id = item.at("id").get<AssetId>();
        const auto& record = catalog.records().at(id);
        if (id == owner || (record.subasset && record.subasset->owner == owner))
            result.push_back(item);
    }
    return result;
}
void source_owner(const AssetCatalog& catalog, const ProjectPaths& paths,
                  const std::filesystem::path& source, AssetId owner) {
    for (const auto& [id, record] : catalog.records())
        if (!record.subasset && paths.same_locator(record.source, source))
            require(id == owner, "Import source is already owned by another logical asset");
}
} // namespace
struct AssetImportService::Pending {
    AssetImportDraft draft;
    PreparePublication prepare;
    AssetPublisher::Compatibility compatibility;
    AssetImportPlan plan;
    bool cache_hit = false;
};
AssetImportService::AssetImportService(std::shared_ptr<const ProjectLease> lease,
                                       std::shared_ptr<const AssetImporterRegistry> registry,
                                       ImportTarget target, unsigned workers)
    : lease_(std::move(lease)), registry_(std::move(registry)), target_(std::move(target)),
      publisher_(required_lease(lease_)), owner_(std::this_thread::get_id()), queue_(workers) {
    require(registry_ && registry_->sealed(), "Asset import registry must be sealed");
    // Refuse new work if an interrupted publication has an unresolved conflict.
    AssetFileTransaction(*lease_).recover();
    publisher_.recover();
}
AssetImportService::~AssetImportService() = default;
void AssetImportService::check() const {
    require(owner_ == std::this_thread::get_id(), "Asset import operation requires owner thread");
    lease_->check();
}
AssetImportDraft AssetImportService::prepare(const std::filesystem::path& source,
                                             std::optional<std::string_view> selected,
                                             AssetId authored_identity) {
    check();
    const ProjectPaths paths(lease_->root());
    auto locator = ProjectPaths::normalize(source);
    const auto path = paths.resolve(locator);
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot open asset source");
    std::vector<std::byte> prefix(65536);
    input.read(reinterpret_cast<char*>(prefix.data()), std::streamsize(prefix.size()));
    require(!input.bad(), "Cannot read asset source prefix");
    prefix.resize(std::size_t(input.gcount()));
    input.close();
    auto catalog = AssetCatalog::open_project(paths.root());
    AssetId owner;
    for (const auto& [id, record] : catalog.records())
        if (!record.subasset && paths.same_locator(record.source, locator)) {
            require(!owner, "Source has ambiguous logical asset ownership");
            owner = id;
            locator = record.source;
        }
    // Repeated edits to a not-yet-published source use the same provisional UUID.
    for (const auto& [job, pending] : pending_) {
        (void)job;
        if (paths.same_locator(pending->draft.request.source, locator)) {
            require(!owner || owner == pending->draft.request.asset,
                    "Source identity changed during import");
            owner = pending->draft.request.asset;
        }
    }
    require(!authored_identity || !owner || authored_identity == owner,
            "Authored source identity differs from the registered/reserved asset");
    auto ticket = publisher_.capture(
        owner ? owner : (authored_identity ? authored_identity : AssetId::generate()), locator);
    std::optional<AssetImportSidecar> sidecar;
    if (ticket.sidecar_bytes) {
        sidecar = AssetImportSidecar::parse(*ticket.sidecar_bytes);
        require(sidecar->identity.source == locator && (!owner || sidecar->identity.owner == owner),
                "Import sidecar source/identity disagrees with catalog");
        require(!authored_identity || sidecar->identity.owner == authored_identity,
                "Authored source identity differs from import sidecar");
        ticket.owner = sidecar->identity.owner;
        if (!selected)
            selected = sidecar->settings.importer;
    }
    auto importer = registry_->select({locator, prefix}, target_, selected);
    if (const auto existing = catalog.records().find(ticket.owner);
        existing != catalog.records().end()) {
        const auto& record = existing->second;
        require(!record.subasset && paths.same_locator(record.source, locator),
                "Import identity belongs to another source; copying a sidecar is not duplication");
        require(std::find(importer->descriptor().output_types.begin(),
                          importer->descriptor().output_types.end(),
                          record.type) != importer->descriptor().output_types.end(),
                "Selected importer cannot replace the registered asset type");
    }
    for (const auto& [job, pending] : pending_) {
        (void)job;
        require(pending->draft.request.asset != ticket.owner ||
                    paths.same_locator(pending->draft.request.source, locator),
                "Import identity is reserved by another active source");
    }
    AssetImportDraft draft;
    draft.importer = importer;
    draft.ticket = ticket;
    draft.request = {ticket.owner,
                     paths.root(),
                     locator,
                     target_,
                     {importer->descriptor().id, importer->settings().version()}};
    if (sidecar) {
        require(sidecar->settings.importer == importer->descriptor().id,
                "Changing a source importer requires an explicit migration");
        draft.request.settings = sidecar->settings;
    }
    (void)importer->settings().effective(draft.request.settings);
    return draft;
}
AssetJobId AssetImportService::submit(AssetImportDraft draft, PreparePublication prepare,
                                      AssetPublisher::Compatibility compatibility, int priority) {
    check();
    require(prepare && compatibility && draft.importer,
            "Import requires preparation and compatibility validation");
    require(draft.request.project == lease_->root() && draft.request.target == target_ &&
                draft.request.asset == draft.ticket.owner &&
                draft.request.source == draft.ticket.source &&
                registry_->find(draft.importer->descriptor().id) == draft.importer,
            "Import draft belongs to another service/provider");
    const auto current = publisher_.capture(draft.ticket.owner, draft.ticket.source);
    require(current.catalog_bytes == draft.ticket.catalog_bytes &&
                current.sidecar_bytes == draft.ticket.sidecar_bytes,
            "Import settings draft is stale; refresh before applying");
    (void)draft.importer->settings().effective(draft.request.settings);
    require(next_generation_ != std::numeric_limits<std::uint64_t>::max(),
            "Import generation exhausted; reopen project");
    auto pending = std::make_shared<Pending>();
    pending->draft = std::move(draft);
    pending->prepare = std::move(prepare);
    pending->compatibility = std::move(compatibility);
    const auto id = queue_.submit(
        pending->draft.request.asset, next_generation_++, {}, priority, {},
        [pending](std::stop_token stop, const AssetBuildQueue::Progress& progress) {
            const auto& importer = *pending->draft.importer;
            const auto& request = pending->draft.request;
            progress(0, "Reading source and dependencies");
            pending->plan = importer.discover(request, stop);
            require(!stop.stop_requested(), "Import cancelled after discovery");
            DerivedDataCache cache(request.project,
                                   asset_detail::import_cache_limits(importer.descriptor()));
            auto validator = [&](const auto& artifact) { importer.validate(artifact); };
            if (auto hit = cache.find(pending->plan.input, validator)) {
                pending->cache_hit = true;
                return std::move(*hit);
            }
            auto files = importer.import_and_cook(request, pending->plan, stop, progress);
            require(!stop.stop_requested(), "Import cancelled before cache publication");
            return cache.publish(pending->plan.input, std::move(files), validator);
        });
    pending_.emplace(id, std::move(pending));
    const auto jobs = queue_.snapshot();
    std::erase_if(receipts_, [&](const auto& receipt) {
        return std::none_of(jobs.begin(), jobs.end(),
                            [&](const auto& job) { return job.id == receipt.first; });
    });
    return id;
}
void AssetImportService::cancel(AssetJobId job) {
    check();
    queue_.cancel(job);
}
std::vector<AssetJobInfo> AssetImportService::jobs() const {
    check();
    auto jobs = queue_.snapshot();
    for (auto& job : jobs) {
        if (const auto found = receipts_.find(job.id); found != receipts_.end())
            job = found->second;
        else if (job.state == AssetJobState::Ready)
            job.stage = "Waiting for publication";
    }
    return jobs;
}
bool AssetImportService::wait_idle(std::chrono::milliseconds timeout) {
    check();
    return queue_.wait_idle(timeout);
}
std::vector<AssetImportOutcome> AssetImportService::poll() {
    check();
    std::vector<AssetImportOutcome> outcomes;
    for (auto& completion : queue_.drain()) {
        const auto found = pending_.find(completion.info.id);
        require(found != pending_.end(), "Unknown asset import completion");
        const auto pending = found->second;
        pending_.erase(found);
        AssetImportOutcome outcome;
        outcome.job = completion.info;
        outcome.cache_hit = pending->cache_hit;
        outcome.diagnostic = completion.info.diagnostic;
        if (completion.info.state == AssetJobState::Ready) {
            try {
                const auto& draft = pending->draft;
                auto ticket = publisher_.capture(draft.ticket.owner, draft.ticket.source);
                require(ticket.sidecar_bytes == draft.ticket.sidecar_bytes,
                        "Import sidecar changed while candidate was building");
                const auto before = catalog_from(lease_->root(), draft.ticket.catalog_bytes);
                const auto current = catalog_from(lease_->root(), ticket.catalog_bytes);
                require(family(before, ticket.owner) == family(current, ticket.owner),
                        "Imported asset selection changed while candidate was building");
                source_owner(current, ProjectPaths(lease_->root()), ticket.source, ticket.owner);
                // Unrelated catalog commits can coexist. This owner's complete family
                // and sidecar must still match; publisher rechecks every dependency.
                AssetPublicationCandidate candidate;
                candidate.ticket = std::move(ticket);
                candidate.input = pending->plan.input;
                if (candidate.ticket.sidecar_bytes)
                    candidate.sidecar = AssetImportSidecar::parse(*candidate.ticket.sidecar_bytes);
                candidate.sidecar.settings = draft.request.settings;
                candidate.sidecar.build_inputs = candidate.input.document();
                candidate.files = completion.artifact->files;
                pending->prepare(candidate, pending->plan, current);
                outcome.publication = publisher_.publish(std::move(candidate), *draft.importer,
                                                         pending->compatibility);
                outcome.published = true;
                outcome.job.stage = "Imported";
                outcome.diagnostic = outcome.publication->cleanup_diagnostic;
            } catch (const std::exception& e) {
                outcome.job.state = AssetJobState::Failed;
                outcome.diagnostic = std::string(e.what()).substr(0, 8192);
                outcome.job.diagnostic = outcome.diagnostic;
                if (const auto* identity = dynamic_cast<const SubassetIdentityFailure*>(&e))
                    outcome.identity_conflicts = identity->conflicts;
            }
        }
        receipts_[outcome.job.id] = outcome.job;
        outcomes.push_back(std::move(outcome));
    }
    return outcomes;
}
} // namespace forge
