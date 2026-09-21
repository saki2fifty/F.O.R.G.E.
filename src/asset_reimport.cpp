#include "asset_reimport.hpp"
namespace forge {
namespace {
const ProjectLease& valid_lease(const std::shared_ptr<const ProjectLease>& value) {
    if (!value)
        throw std::runtime_error("Automatic reimport requires project writer ownership");
    value->check();
    return *value;
}
nlohmann::json revision(const AssetRecord& record) {
    const auto it = record.metadata.find("forge.import");
    return it == record.metadata.end() ? nlohmann::json{} : *it;
}
} // namespace
AssetReimportService::AssetReimportService(std::shared_ptr<const ProjectLease> lease,
                                           std::vector<AssetReimportRoute> profiles,
                                           SourceScanOptions options,
                                           std::chrono::milliseconds interval,
                                           std::chrono::milliseconds debounce)
    : lease_(std::move(lease)), catalog_(std::make_shared<const AssetCatalog>(
                                    AssetCatalog::open_project(valid_lease(lease_).root()))),
      watch_(lease_->root(), std::move(options), interval, debounce) {
    if (profiles.empty() || profiles.size() > 64)
        throw std::runtime_error("Automatic importer route count is outside bounds");
    std::set<std::string> ids;
    for (auto& profile : profiles) {
        if (!profile.registry || !profile.registry->sealed() || !profile.prepare ||
            !profile.registry->find(profile.importer) || !ids.insert(profile.importer).second)
            throw std::runtime_error("Invalid/duplicate automatic importer route");
        routes_.push_back({std::move(profile), {}});
    }
}
void AssetReimportService::check() const {
    if (std::this_thread::get_id() != owner_)
        throw std::runtime_error("Automatic reimport requires its owning thread");
    lease_->check();
}
std::optional<std::size_t> AssetReimportService::route(const AssetRecord& record) const {
    if (record.subasset)
        return {};
    const auto selected = revision(record);
    if (!selected.is_object() || !selected.contains("importer") ||
        !selected.at("importer").is_string())
        return {};
    for (std::size_t i = 0; i < routes_.size(); ++i)
        if (routes_[i].profile.importer == selected.at("importer").get<std::string>())
            return i;
    return {};
}
AssetId AssetReimportService::root(AssetId id) const {
    const auto found = catalog_->records().find(id);
    if (found == catalog_->records().end())
        return {};
    return found->second.subasset ? found->second.subasset->owner : id;
}
void AssetReimportService::enqueue(AssetId id) {
    id = root(id);
    if (id && route(catalog_->records().at(id)))
        queue_.insert(id);
}
void AssetReimportService::dependents(AssetId id) {
    for (const auto dependent : catalog_->dependency_graph().invalidated_by(id))
        enqueue(dependent);
}
void AssetReimportService::cancel_active() {
    if (!active_ || active_->superseded)
        return;
    routes_[active_->route].service->cancel(active_->job);
    active_->superseded = true;
    enqueue(active_->asset);
}
void AssetReimportService::rescan(bool retry_failed) {
    check();
    watch_.rescan();
    if (retry_failed) {
        for (const auto id : failed_)
            enqueue(id);
        failed_.clear();
    }
}
void AssetReimportService::reimport(const std::vector<AssetId>& assets) {
    check();
    if (suspended_)
        throw std::runtime_error("Finish the current source-file operation before reimporting");
    if (assets.empty() || assets.size() > 4096)
        throw std::runtime_error("Select between 1 and 4096 registered assets to reimport");
    std::set<AssetId> owners;
    for (const auto id : assets) {
        const auto owner = root(id);
        if (!owner || !route(catalog_->records().at(owner)))
            throw std::runtime_error("No published importer route for selected asset " + id.str() +
                                     "; open its source/import document first");
        owners.insert(owner);
    }
    // Existing blocked-draft, dependency ordering, candidate and stale-generation rules apply.
    auto queued = queue_;
    queued.insert(owners.begin(), owners.end());
    queue_.swap(queued);
    for (const auto id : owners)
        failed_.erase(id);
    watch_.rescan();
}
void AssetReimportService::suspend(bool value) {
    check();
    if (suspended_ == value)
        return;
    suspended_ = value;
    if (value)
        cancel_active();
    else
        rescan(false);
}
void AssetReimportService::catalog_changed(std::shared_ptr<const AssetCatalog> next) {
    check();
    if (!next || next == catalog_)
        return;
    std::vector<AssetId> changed;
    for (const auto& [id, record] : next->records()) {
        const auto prior = catalog_->records().find(id);
        if (prior == catalog_->records().end() || revision(prior->second) != revision(record))
            changed.push_back(id);
    }
    // Removal is absent from the new records, but its old Build dependents still
    // need admission/diagnostics. Retain that invalidation evidence before swapping.
    std::set<AssetId> removed_dependents;
    for (const auto& [id, record] : catalog_->records()) {
        (void)record;
        if (!next->records().contains(id))
            for (const auto dependent : catalog_->dependency_graph().invalidated_by(id))
                removed_dependents.insert(dependent);
    }
    if (active_ && !next->records().contains(active_->asset))
        cancel_active();
    catalog_ = std::move(next);
    std::erase_if(queue_, [&](auto id) { return !catalog_->records().contains(id); });
    std::erase_if(failed_, [&](auto id) { return !catalog_->records().contains(id); });
    for (const auto id : removed_dependents)
        enqueue(id);
    for (const auto id : changed) {
        if (active_ && root(id) == active_->asset)
            cancel_active();
        dependents(id);
    }
}
bool AssetReimportService::source_current(const AssetRecord& record) const {
    if (!sources_ || !sources_->complete)
        return false;
    const auto source = sources_->files.find(record.source);
    const auto sidecar = sources_->files.find(AssetPublisher::sidecar_path(record.source));
    const auto imported = revision(record);
    const auto selected = route(record);
    if (source == sources_->files.end() || sidecar == sources_->files.end() ||
        !imported.is_object() || !selected ||
        imported.value("source_digest", std::string{}) != source->second.digest ||
        imported.value("sidecar_digest", std::string{}) != sidecar->second.digest)
        return false;
    const auto& profile = routes_[*selected].profile;
    const auto& descriptor = profile.registry->find(profile.importer)->descriptor();
    if (imported.value("importer_revision", std::string{}) != descriptor.revision ||
        imported.value("platform", std::string{}) != profile.target.platform ||
        imported.value("backend", std::string{}) != profile.target.backend ||
        imported.value("profile", std::string{}) != profile.target.profile)
        return false;
    for (const auto& dependency : record.source_dependencies) {
        const auto found = sources_->files.find(dependency.source);
        if (found == sources_->files.end() || found->second.digest != dependency.revision)
            return false;
    }
    for (const auto& dependency : record.dependency_edges) {
        if (dependency.kind == AssetDependencyKind::Subasset ||
            (dependency.kind == AssetDependencyKind::Optional && dependency.revision.empty()))
            continue;
        const auto found = catalog_->records().find(dependency.target);
        if (found == catalog_->records().end() || found->second.type != dependency.expected_type ||
            (found->second.subasset && found->second.subasset->removed) ||
            revision(found->second).value("key", std::string{}) != dependency.revision)
            return false;
    }
    return true;
}
void AssetReimportService::observe(const AssetWatchUpdate& update) {
    sources_ = update.snapshot;
    if (update.generation != observed_) {
        // Observation invalidates in-flight work before debounce delivery. The
        // publisher still verifies captured source/dependency bytes at commit.
        cancel_active();
        observed_ = update.generation;
    }
    if (!sources_ || !sources_->complete || update.changes.empty())
        return;
    std::map<std::filesystem::path, std::vector<AssetId>, ProjectLocatorLess> by_source;
    for (const auto& [id, record] : catalog_->records())
        if (!record.subasset) {
            by_source[record.source].push_back(id);
            by_source[AssetPublisher::sidecar_path(record.source)].push_back(id);
        }
    for (const auto& change : update.changes) {
        for (const auto& source : {change.source, change.previous_source}) {
            if (source.empty())
                continue;
            if (const auto found = by_source.find(source); found != by_source.end())
                for (const auto id : found->second) {
                    const auto& record = catalog_->records().at(id);
                    // Compare the committed sidecar as well as source bytes:
                    // unchanged startup scans and self-writes need no rebuild.
                    if (!source_current(record))
                        enqueue(id);
                }
            for (const auto id : catalog_->dependency_graph().source_referrers(source))
                if (const auto owner = root(id);
                    owner && !source_current(catalog_->records().at(owner)))
                    enqueue(owner);
        }
    }
}
std::map<AssetId, AssetJobState> AssetReimportService::activity() const {
    check();
    std::map<AssetId, AssetJobState> result;
    for (const auto id : failed_)
        result[id] = AssetJobState::Failed;
    for (const auto id : queue_)
        result[id] = AssetJobState::Queued;
    if (active_)
        result[active_->asset] = AssetJobState::Running;
    return result;
}
std::vector<AssetJobInfo> AssetReimportService::jobs() const {
    check();
    return active_ ? routes_[active_->route].service->jobs() : std::vector<AssetJobInfo>{};
}
std::vector<AssetImportOutcome> AssetReimportService::poll() {
    check();
    if (!suspended_)
        if (auto update = watch_.poll())
            observe(*update);
    if (active_ && blocked && blocked(active_->asset))
        cancel_active();
    std::vector<AssetImportOutcome> outcomes;
    if (active_) {
        auto& selected = routes_[active_->route];
        for (auto& outcome : selected.service->poll()) {
            if (outcome.job.id != active_->job)
                throw std::runtime_error("Unexpected automatic import receipt");
            const auto completed = *active_;
            active_.reset();
            if (outcome.published) {
                failed_.erase(completed.asset);
                catalog_changed(std::make_shared<const AssetCatalog>(outcome.publication->catalog));
                const auto& record = catalog_->records().at(completed.asset);
                watch_.acknowledge_write(record.source,
                                         revision(record).at("source_digest").get<std::string>());
                for (const auto& [path, digest] : outcome.publication->written_sources)
                    watch_.acknowledge_write(path, digest);
            } else if (!completed.superseded)
                failed_.insert(completed.asset);
            if (!completed.superseded)
                outcomes.push_back(std::move(outcome));
        }
    }
    if (suspended_ || active_ || !watch_.complete() || queue_.empty())
        return outcomes;
    // Bound UI-thread scheduling work, including when many documents are held
    // dirty. Round-robin consideration avoids a blocked prefix starving others.
    const auto count = std::min<std::size_t>(64, queue_.size());
    for (std::size_t attempt = 0; attempt < count; ++attempt) {
        auto position = queue_.upper_bound(cursor_);
        if (position == queue_.end())
            position = queue_.begin();
        const auto id = cursor_ = *position;
        if (blocked && blocked(id))
            continue;
        const auto& dependencies = catalog_->dependency_graph().dependencies(id);
        const bool waiting =
            std::any_of(dependencies.begin(), dependencies.end(), [&](const auto& edge) {
                const auto owner = root(edge.target);
                return edge.kind == AssetDependencyKind::Build && owner != id &&
                       queue_.contains(owner);
            });
        if (waiting)
            continue;
        queue_.erase(id);
        const auto& record = catalog_->records().at(id);
        const auto selected = route(record);
        if (!selected)
            continue;
        try {
            auto& entry = routes_[*selected];
            if (!entry.service)
                entry.service = std::make_unique<AssetImportService>(lease_, entry.profile.registry,
                                                                     entry.profile.target, 1);
            auto draft = entry.service->prepare(record.source, entry.profile.importer, id);
            const auto job = entry.service->submit(std::move(draft), entry.profile.prepare,
                                                   [](const auto&, const auto&) {});
            active_ = Active{id, *selected, job, false};
        } catch (const std::exception& e) {
            AssetImportOutcome failure;
            failure.job.asset = id;
            failure.job.state = AssetJobState::Failed;
            failure.diagnostic = e.what();
            failed_.insert(id);
            outcomes.push_back(std::move(failure));
        }
        break; // Bound owner-thread admissions to one per poll.
    }
    return outcomes;
}
} // namespace forge
