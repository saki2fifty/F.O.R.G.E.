#include "ui_asset_catalog.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include <algorithm>
#include <set>
namespace forge {
AssetCatalog refresh_ui_asset_catalog(const ProjectLease& lease, const UiAssetSnapshot& observed) {
    lease.check();
    const ProjectPaths paths(lease.root());
    if (paths.root() != ProjectPaths(observed.project).root() || observed.sources.empty() ||
        observed.sources.size() > 32 || observed.documents.size() > 16)
        throw std::runtime_error("UI resource snapshot has a different project or invalid size");
    const auto index = AssetCatalog::project_index(paths.root());
    const auto before = asset_storage::read(index, max_asset_index_bytes);
    auto catalog = AssetCatalog::open_project(paths.root());
    UiResources admitted(paths.root());
    std::set<std::filesystem::path, ProjectLocatorLess> unique;
    for (const auto& source : observed.sources) {
        const auto path = ProjectPaths::normalize(source.source);
        if (!unique.insert(path).second)
            throw std::runtime_error("Duplicate UI source in observation");
        (void)admitted.read(path_utf8(path));
    }
    const auto current = admitted.snapshot();
    // Re-admit exact bytes/metadata. A stale successful presenter must never
    // publish a newer unvalidated file as if it were the observed resource.
    auto expected = observed.sources;
    std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) {
        return path_utf8(a.source) < path_utf8(b.source);
    });
    if (expected != current.sources)
        throw std::runtime_error("UI sources changed since admission; reload the UI and retry");
    auto records = catalog.records();
    std::map<std::filesystem::path, AssetId, ProjectLocatorLess> identities;
    for (const auto& source : current.sources) {
        AssetRecord record{AssetId::generate(), source.type, source.source, 1, {}};
        for (const auto& [id, existing] : records) {
            if (existing.subasset || !paths.same_locator(existing.source, source.source))
                continue;
            if (identities.contains(source.source))
                throw std::runtime_error("UI source has ambiguous catalog identities");
            if (existing.type != source.type)
                throw std::runtime_error("UI source is already registered as another asset type");
            record = existing;
            identities.emplace(source.source, id);
        }
        identities[source.source] = record.id;
        // Preserve other importers' metadata and selection. Native RmlUi still
        // reads its supported source image, not an incompatible cooked texture.
        record.metadata["forge.ui_source"] = source.details;
        record.metadata["forge.ui_source"]["bytes"] = source.bytes;
        record.metadata["forge.ui_source"]["digest"] = source.digest;
        std::erase_if(record.source_dependencies,
                      [](const auto& e) { return e.role == "ui.source"; });
        record.source_dependencies.push_back({source.source, "ui.source", source.digest});
        records[record.id] = std::move(record);
    }
    for (const auto& [id, source] : observed.documents) {
        const auto found = identities.find(source);
        if (found == identities.end() || found->second != id ||
            records.at(id).type != UiDocumentAsset::type)
            throw std::runtime_error("Observed UI document identity/source changed");
        auto& record = records.at(id);
        if (record.dependency_edges.empty() && !record.dependencies.empty()) {
            for (const auto target : record.dependencies) {
                const auto prior = records.find(target);
                if (prior == records.end())
                    throw std::runtime_error(
                        "UI dependency has no known type; repair it before refresh");
                record.dependency_edges.push_back(
                    {target, prior->second.type, AssetDependencyKind::Runtime, "legacy", {}});
            }
        }
        std::erase_if(record.dependency_edges,
                      [](const auto& edge) { return edge.role == "ui.observed"; });
        std::erase_if(record.source_dependencies,
                      [](const auto& edge) { return edge.role == "ui.observed"; });
        for (const auto& dependency : current.sources) {
            // RmlUi may share resources/caches between simultaneous documents.
            // Conservatively index all non-document resources in this good set.
            // Do not invent document-to-document cycles or parse markup twice.
            if (dependency.type == UiDocumentAsset::type)
                continue;
            const auto target = identities.at(dependency.source);
            record.dependency_edges.push_back(
                {target, records.at(target).type, AssetDependencyKind::Runtime, "ui.observed", {}});
            record.source_dependencies.push_back(
                {dependency.source, "ui.observed", dependency.digest});
        }
        std::set<AssetId> targets;
        for (const auto& edge : record.dependency_edges)
            targets.insert(edge.target);
        record.dependencies.assign(targets.begin(), targets.end());
    }
    std::vector<AssetRecord> replacement;
    for (auto& [id, record] : records) {
        (void)id;
        replacement.push_back(std::move(record));
    }
    catalog.replace_all(std::move(replacement));
    lease.check();
    if (asset_storage::read(index, max_asset_index_bytes) != before)
        throw std::runtime_error("Asset catalog changed; refresh UI resources and retry");
    // One atomic catalog publication; no source edits or scene Undo operation.
    catalog.save(index);
    return catalog;
}
AssetRecord register_ui_source(const ProjectLease& lease, const std::filesystem::path& source) {
    UiResources admitted(lease.root());
    const auto path = ProjectPaths::normalize(source);
    (void)admitted.read(path_utf8(path));
    const auto catalog = refresh_ui_asset_catalog(lease, admitted.snapshot());
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (!record.subasset && ProjectPaths(lease.root()).same_locator(path, record.source))
            return record;
    }
    throw std::runtime_error("Registered UI source is missing");
}
} // namespace forge
