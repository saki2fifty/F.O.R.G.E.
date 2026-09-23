#include "runtime_dependencies.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "ui_asset_catalog.hpp"
#include <forge/project.hpp>
#include <set>
namespace forge {
using Json = nlohmann::json;
namespace {
Json revisions(const std::filesystem::path& root, const AssetRecord& record) {
    const ProjectPaths paths(root);
    std::set<std::filesystem::path> sources{record.source};
    for (const auto& source : record.source_dependencies) {
        const auto extension = source.source.extension();
        if (extension == ".rml" || extension == ".rcss")
            sources.insert(source.source);
    }
    for (const auto& edge : record.dependency_edges) {
        constexpr std::string_view prefix = "declared:module/";
        if (!edge.role.starts_with(prefix))
            continue;
        const auto end = edge.role.find('/', prefix.size());
        if (end == std::string::npos || end + 1 == edge.role.size())
            throw std::runtime_error(
                "export.declarations.module: Module declaration needs owner and reason");
        const auto module_id = edge.role.substr(prefix.size(), end - prefix.size());
        const auto settings = ProjectSettings(root).document();
        bool found = false;
        for (const auto& module : settings.value("modules", Json::array()))
            if (module.is_object() && module.at("id") == module_id) {
                sources.insert(std::filesystem::u8path(module.at("library").get<std::string>()));
                found = true;
            }
        if (!found)
            throw std::runtime_error("export.declarations.module: Native module owner is absent: " +
                                     module_id);
        sources.insert("forge.project.json");
    }
    Json result = Json::object();
    std::size_t total = 0;
    for (const auto& source : sources) {
        auto bytes = asset_detail::read_bytes(paths.resolve(source), 64 * 1024 * 1024);
        if (bytes.size() > 256 * 1024 * 1024 - total)
            throw std::runtime_error("export.declarations.limit: Source revisions exceed 256 MiB");
        total += bytes.size();
        result[path_utf8(source)] = asset_detail::content_digest(bytes);
    }
    return result;
}
} // namespace
void validate_runtime_declarations(const std::filesystem::path& root, const AssetRecord& record) {
    const bool declared = std::any_of(record.dependency_edges.begin(),
                                      record.dependency_edges.end(), declared_runtime_edge);
    const auto found = record.metadata.find("forge.runtime_declarations");
    if (!declared && found == record.metadata.end())
        return;
    if (found == record.metadata.end() || found->at("version") != 1 ||
        found->at("sources") != revisions(root, record))
        throw std::runtime_error("export.declarations.stale: Review Runtime Dependencies for " +
                                 record.id.str());
}
AssetCatalog declare_runtime_dependencies(const ProjectLease& lease, AssetId owner,
                                          std::vector<AssetDependency> edges,
                                          const Json& expected_catalog,
                                          const UiAssetSnapshot* prepared_ui) {
    lease.check();
    auto catalog = AssetCatalog::open_project(lease.root());
    if (catalog.document() != expected_catalog)
        throw std::runtime_error(
            "export.declarations.conflict: Catalog changed; refresh and retry");
    if (prepared_ui) {
        if (prepared_ui->documents.size() != 1 || !prepared_ui->documents.contains(owner))
            throw std::runtime_error("export.declarations.owner: UI preparation has wrong owner");
        catalog = prepare_ui_asset_catalog(std::move(catalog), lease.root(), *prepared_ui);
    }
    const auto found = catalog.records().find(owner);
    if (found == catalog.records().end() || found->second.subasset)
        throw std::runtime_error("export.declarations.owner: Select an authored owner asset");
    if (edges.size() > 4096)
        throw std::runtime_error("export.declarations.limit: At most 4096 concrete dependencies");
    auto record = found->second;
    std::erase_if(record.dependency_edges, declared_runtime_edge);
    for (auto& edge : edges) {
        if (!declared_runtime_edge(edge) || edge.role.size() <= 9 || edge.role.size() > 128 ||
            edge.target == owner || !edge.revision.empty())
            throw std::runtime_error(
                "export.declarations.invalid: Expected required typed dependency and reason");
        const auto target = catalog.resolve(edge.target, edge.expected_type);
        if (target.state != AssetState::Available)
            throw std::runtime_error("export.declarations.missing: " + target.diagnostic);
        record.dependency_edges.push_back(std::move(edge));
    }
    std::sort(record.dependency_edges.begin(), record.dependency_edges.end());
    record.dependency_edges.erase(
        std::unique(record.dependency_edges.begin(), record.dependency_edges.end()),
        record.dependency_edges.end());
    std::set<AssetId> ids;
    for (const auto& edge : record.dependency_edges)
        ids.insert(edge.target);
    // Never reinterpret old untyped metadata or observed edges as declarations.
    for (const auto id : record.dependencies)
        if (!ids.contains(id) &&
            std::none_of(
                found->second.dependency_edges.begin(), found->second.dependency_edges.end(),
                [&](const auto& edge) { return declared_runtime_edge(edge) && edge.target == id; }))
            throw std::runtime_error(
                "export.declarations.untyped: Repair untyped dependency metadata");
    record.dependencies.assign(ids.begin(), ids.end());
    record.metadata["forge.runtime_declarations"] = {{"version", 1},
                                                     {"sources", revisions(lease.root(), record)}};
    catalog.replace(record);
    lease.check();
    if (AssetCatalog::open_project(lease.root()).document() != expected_catalog)
        throw std::runtime_error("export.declarations.conflict: Catalog changed before save");
    validate_runtime_declarations(lease.root(), record);
    catalog.save(AssetCatalog::project_index(lease.root()));
    return catalog;
}
} // namespace forge
