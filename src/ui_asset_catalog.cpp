#include "ui_asset_catalog.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "ui_resource_rules.hpp"
#include <algorithm>
#include <cctype>
#include <forge/texture_asset.hpp>
#include <set>
namespace forge {
namespace {
const AssetRecord* source_record(const std::map<AssetId, AssetRecord>& records,
                                 const ProjectPaths& paths, const std::filesystem::path& source,
                                 const std::string& type) {
    const AssetRecord* result = nullptr;
    for (const auto& [id, record] : records) {
        if (record.subasset || !paths.same_locator(record.source, source))
            continue;
        if (result)
            throw std::runtime_error("UI source has ambiguous catalog identities");
        if (record.type != type)
            throw std::runtime_error("UI source is already registered as another asset type");
        result = &record;
    }
    return result;
}
} // namespace
bool ui_image_source_candidate(const AssetRecord& record) {
    auto extension = record.source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return record.type == TextureAsset::type && !record.subasset &&
           ui_resource_detail::image_extension(extension);
}
nlohmann::json ui_image_reference(const AssetCatalog& catalog,
                                  const std::filesystem::path& document, AssetId asset) {
    const auto found = catalog.records().find(asset);
    if (found == catalog.records().end() || !ui_image_source_candidate(found->second))
        throw std::runtime_error("Choose a registered standalone project image source");
    const auto resolved = catalog.resolve(asset, TextureAsset::type);
    if (resolved.state != AssetState::Available)
        throw std::runtime_error("The selected project image is unavailable");
    const auto source = ProjectPaths::normalize(found->second.source);
    const auto base = ProjectPaths::normalize(document);
    const ProjectPaths paths(catalog.project_root());
    if (const auto* unique = source_record(catalog.records(), paths, source, TextureAsset::type);
        !unique || unique->id != asset)
        throw std::runtime_error("UI image source has no unique project identity");
    const auto reference = path_utf8(source.lexically_relative(base.parent_path()));
    if (ui_resource_detail::join(path_utf8(base), reference) != path_utf8(source))
        throw std::runtime_error("Image source has no contained document-relative reference");
    UiResources admitted(catalog.project_root());
    (void)admitted.read(path_utf8(source));
    const auto info = admitted.snapshot().sources.at(0);
    return {{"asset", asset},
            {"source", path_utf8(source)},
            {"reference", reference},
            {"details", info.details}};
}
AssetCatalog prepare_ui_asset_catalog(AssetCatalog catalog, const std::filesystem::path& root,
                                      const UiAssetSnapshot& observed) {
    const ProjectPaths paths(root);
    if (paths.root() != ProjectPaths(observed.project).root() || observed.sources.empty() ||
        observed.sources.size() > 32 || observed.documents.size() > 16)
        throw std::runtime_error("UI resource snapshot has a different project or invalid size");
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
    for (const auto& source : observed.automatic_sources)
        if (std::none_of(current.sources.begin(), current.sources.end(),
                         [&](const auto& s) { return s.source == source; }))
            throw std::runtime_error("Automatic UI dependency was not admitted");
    auto records = catalog.records();
    std::map<std::filesystem::path, AssetId, ProjectLocatorLess> identities;
    for (const auto& source : current.sources) {
        AssetRecord record;
        record.id = AssetId::generate();
        record.type = source.type;
        record.source = source.source;
        if (const auto* existing = source_record(records, paths, source.source, source.type))
            record = *existing;
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
        std::erase_if(record.dependency_edges, [](const auto& edge) {
            return edge.role == "ui.observed" || edge.role == "ui.automatic";
        });
        std::erase_if(record.source_dependencies, [](const auto& edge) {
            return edge.role == "ui.observed" || edge.role == "ui.automatic";
        });
        for (const auto& dependency : current.sources) {
            // RmlUi may share resources/caches between simultaneous documents.
            // Conservatively index all non-document resources in this good set.
            // Do not invent document-to-document cycles or parse markup twice.
            if (dependency.type == UiDocumentAsset::type)
                continue;
            const auto target = identities.at(dependency.source);
            const bool automatic = observed.automatic_sources.contains(dependency.source);
            record.dependency_edges.push_back(
                {target,
                 records.at(target).type,
                 automatic ? AssetDependencyKind::Runtime : AssetDependencyKind::Optional,
                 automatic ? "ui.automatic" : "ui.observed",
                 {}});
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
    return catalog;
}
AssetCatalog refresh_ui_asset_catalog(const ProjectLease& lease, const UiAssetSnapshot& observed) {
    lease.check();
    const auto index = AssetCatalog::project_index(lease.root());
    const auto before = asset_storage::read(index, max_asset_index_bytes);
    auto catalog =
        prepare_ui_asset_catalog(AssetCatalog::open_project(lease.root()), lease.root(), observed);
    lease.check();
    if (asset_storage::read(index, max_asset_index_bytes) != before)
        throw std::runtime_error("Asset catalog changed; refresh UI resources and retry");
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
