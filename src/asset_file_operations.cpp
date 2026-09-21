#include "asset_file_operations.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "gltf_container.hpp"
#include <forge/asset_publication.hpp>
#include <forge/material_source.hpp>
#include <forge/prefab.hpp>
#include <forge/scene.hpp>
#include <forge/scene_identity.hpp>
#include <forge/shader_asset.hpp>
namespace forge {
namespace {
using Json = nlohmann::json;
auto text(std::string value) { return std::make_shared<const std::string>(std::move(value)); }
auto read_source(const ProjectPaths& paths, const std::filesystem::path& path,
                 std::size_t limit = 512ull * 1024 * 1024) {
    auto value = asset_storage::read(paths.resolve(path), limit);
    return value ? text(std::move(*value)) : std::shared_ptr<const std::string>{};
}
void cancel(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Asset file operation preparation cancelled");
}
void remap(AssetId& id, const std::map<AssetId, AssetId>& identities) {
    if (const auto found = identities.find(id); found != identities.end())
        id = found->second;
}
std::string lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
} // namespace
AssetFilePlan prepare_asset_file_operation(const std::filesystem::path& project,
                                           AssetFileRequest request,
                                           const AssetSourceRewrite& rewrite,
                                           std::stop_token stop) {
    cancel(stop);
    const ProjectPaths paths(project);
    const auto index = read_source(paths, "forge.assets.json", max_asset_index_bytes);
    if (!index)
        throw std::runtime_error("Register this asset before using catalog file operations");
    AssetCatalog catalog(project);
    catalog.restore(
        asset_detail::parse_bounded_json(std::as_bytes(std::span(*index)), max_asset_index_bytes));
    const auto found = catalog.records().find(request.asset);
    if (found == catalog.records().end())
        throw std::runtime_error("Asset is not registered in this project catalog");
    const auto& owner = found->second;
    if (owner.subasset)
        throw std::runtime_error("Operate on the containing source asset, not an imported member");
    AssetFilePlan result;
    result.request = request;
    result.result = request.asset;
    result.affected = catalog.members(owner.id, true);
    result.affected.push_back(owner.id);
    const std::set<AssetId> family(result.affected.begin(), result.affected.end());
    std::set<AssetId> dependents;
    for (const auto id : result.affected)
        for (const auto dependent : catalog.dependency_graph().referrers(id))
            if (!family.contains(dependent))
                dependents.insert(dependent);
    for (const auto dependent : catalog.dependency_graph().source_referrers(owner.source))
        if (!family.contains(dependent))
            dependents.insert(dependent);
    result.catalog_dependents.assign(dependents.begin(), dependents.end());
    result.warnings.push_back("Catalog dependents do not include every reference in unopened "
                              "scenes, prefabs or opaque extension payloads.");
    if (!dependents.empty() && request.action != AssetFileAction::Duplicate)
        result.warnings.push_back("Dependent asset/source references may need repair. Persistent "
                                  "references are never retargeted by filename.");
    const auto source = read_source(paths, owner.source);
    const auto sidecar_path = AssetPublisher::sidecar_path(owner.source);
    const auto sidecar_bytes = read_source(paths, sidecar_path, max_asset_index_bytes);
    std::optional<AssetImportSidecar> sidecar;
    if (sidecar_bytes) {
        sidecar = AssetImportSidecar::parse(*sidecar_bytes);
        if (sidecar->identity.owner != owner.id || sidecar->identity.source != owner.source)
            throw std::runtime_error("Source sidecar does not belong to the selected asset");
        std::set<AssetId> mapped{owner.id};
        for (const auto& entry : sidecar->identity.entries)
            mapped.insert(entry.id);
        if (mapped != family)
            throw std::runtime_error("Source sidecar/catalog member identities disagree");
        result.importer = sidecar->settings.importer;
    } else if (family.size() != 1)
        throw std::runtime_error("Imported asset family needs its identity sidecar");
    std::vector<AssetRecord> records;
    for (const auto& [id, record] : catalog.records())
        if (!family.contains(id) || request.action == AssetFileAction::Duplicate)
            records.push_back(record);
    if (request.action == AssetFileAction::Delete) {
        if (!request.destination.empty())
            throw std::runtime_error("Delete does not accept a destination");
        if (source)
            result.changes.push_back({owner.source, source, {}});
        if (sidecar_bytes)
            result.changes.push_back({sidecar_path, sidecar_bytes, {}});
    } else {
        if (!source)
            throw std::runtime_error("Selected asset source is missing");
        const auto destination = ProjectPaths::normalize(request.destination);
        if (paths.same_locator(owner.source, destination) ||
            lower(owner.source.extension().string()) != lower(destination.extension().string()))
            throw std::runtime_error("Choose a different path with the same source format suffix");
        for (const auto& [id, record] : catalog.records()) {
            (void)id;
            if (paths.same_locator(record.source, destination))
                throw std::runtime_error("Destination already belongs to a logical asset");
        }
        const auto next_sidecar = AssetPublisher::sidecar_path(destination);
        if (read_source(paths, destination) ||
            read_source(paths, next_sidecar, max_asset_index_bytes))
            throw std::runtime_error(
                "Destination source or sidecar already exists; not overwritten");
        result.request.destination = destination;
        if (request.action == AssetFileAction::Duplicate) {
            if (sidecar) {
                auto copy = duplicate_subasset_identity(sidecar->identity, destination);
                result.duplicated = std::move(copy.old_to_new);
                sidecar->identity = std::move(copy.document);
            } else
                result.duplicated.emplace(owner.id, AssetId::generate());
            result.result = result.duplicated.at(owner.id);
        } else if (request.action != AssetFileAction::Move)
            throw std::runtime_error("Unknown asset file operation");
        // Even a move can require URI rebasing. Formats without an adapter may
        // only rename within their current folder; copies require typed identity proof.
        if (!rewrite && (request.action == AssetFileAction::Duplicate ||
                         destination.parent_path() != owner.source.parent_path()))
            throw std::runtime_error("This source format needs an identity/locator copy adapter");
        const auto rewritten =
            rewrite ? text(rewrite(owner, *source, destination, result.duplicated)) : source;
        cancel(stop);
        if (request.action == AssetFileAction::Move)
            result.changes.push_back({owner.source, source, {}});
        result.changes.push_back({destination, {}, rewritten});
        if (sidecar) {
            sidecar->identity.source = destination;
            sidecar->identity.source_digest =
                asset_detail::content_digest(std::as_bytes(std::span(*rewritten)));
            if (request.action == AssetFileAction::Duplicate)
                sidecar->build_inputs = Json::object(); // No inherited cooked selection.
            const auto next = text(sidecar->document().dump(2));
            if (request.action == AssetFileAction::Move)
                result.changes.push_back({sidecar_path, sidecar_bytes, {}});
            result.changes.push_back({next_sidecar, {}, next});
        }
        for (const auto id : result.affected) {
            auto record = catalog.records().at(id);
            record.source = destination;
            if (request.action == AssetFileAction::Duplicate) {
                remap(record.id, result.duplicated);
                if (record.subasset) {
                    record.subasset->owner = result.result;
                    const auto& entries = sidecar->identity.entries;
                    const auto entry =
                        std::find_if(entries.begin(), entries.end(),
                                     [&](const auto& e) { return e.id == record.id; });
                    if (entry == entries.end())
                        throw std::runtime_error("Duplicated member has no identity mapping");
                    record.subasset->key = entry->key;
                }
                for (auto& dependency : record.dependencies)
                    remap(dependency, result.duplicated);
                for (auto& edge : record.dependency_edges) {
                    const bool internal = result.duplicated.contains(edge.target);
                    remap(edge.target, result.duplicated);
                    if (internal)
                        edge.revision.clear();
                }
                // These are selection/provenance claims, not user extension payloads.
                for (const auto* field :
                     {"forge.import", "forge.model", "forge.texture", "forge.shader"})
                    record.metadata.erase(field);
            }
            records.push_back(std::move(record));
        }
        if (request.action == AssetFileAction::Duplicate && !result.importer.empty())
            result.warnings.push_back(
                "The duplicate has new identities and needs its own validated "
                "import. It does not reuse the original cooked binding.");
    }
    AssetCatalog candidate(project);
    candidate.replace_all(std::move(records));
    result.changes.push_back({"forge.assets.json", index, text(candidate.document().dump(2))});
    cancel(stop);
    return result;
}
std::string rewrite_authored_asset(const AssetRecord& record, std::string_view bytes,
                                   const std::filesystem::path&,
                                   const std::map<AssetId, AssetId>& identities) {
    if (record.type != "scene" && record.type != "prefab" && record.type != "material" &&
        record.type != "shader")
        throw std::runtime_error("No authored-document copy adapter for this source format");
    auto value =
        asset_detail::parse_bounded_json(std::as_bytes(std::span(bytes)), max_asset_index_bytes);
    if (value.at("asset_id").get<AssetId>() != record.id)
        throw std::runtime_error("Authored source identity differs from its catalog record");
    if (record.type == "scene") {
        if (!identities.empty())
            value = duplicate_scene_asset(value, identities.at(record.id));
        Scene::validate_document(value);
    } else if (record.type == "prefab") {
        PrefabDocument::validate(value);
        if (!identities.empty()) {
            value = PrefabDocument(value).duplicate().source;
            value["asset_id"] = identities.at(record.id);
        }
    } else {
        if (!identities.empty())
            value["asset_id"] = identities.at(record.id);
        if (record.type == "material")
            MaterialSource{value}.validate();
        else
            (void)shader_program_source(value);
    }
    return identities.empty() ? std::string(bytes) : value.dump(2);
}
AssetSourceRewrite project_asset_file_rewriter(const std::filesystem::path& project) {
    return [paths = ProjectPaths(project)](const AssetRecord& record, std::string_view bytes,
                                           const std::filesystem::path& destination,
                                           const std::map<AssetId, AssetId>& identities) {
        if (record.type == "model" || record.type == "animation_source")
            return gltf_detail::relocate_gltf_source(paths, record.source, destination,
                                                     std::as_bytes(std::span(bytes)));
        if (record.type == "texture" || record.type == "audio_clip")
            return std::string(bytes); // Catalog-owned identity; no UUID in pixels/WAV.
        return rewrite_authored_asset(record, bytes, destination, identities);
    };
}
} // namespace forge
