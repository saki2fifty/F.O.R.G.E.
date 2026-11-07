#include "runtime_document_package.hpp"
#include "asset_bytes.hpp"
#include "asset_reference_impact.hpp"
#include "bounded_json.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_reflection_profile.hpp"
#include "ui_asset_catalog.hpp"
#include "ui_surface_limits.hpp"
#include <forge/animation_components.hpp>
#include <forge/asset_discovery.hpp>
#include <forge/engine_assets.hpp>
#include <forge/fog_volume.hpp>
#include <forge/input_message.hpp>
#include <forge/mesh_wind.hpp>
#include <forge/prefab.hpp>
#include <forge/scene.hpp>
namespace forge::package_detail {
Json document_runtime_features(const Json& document) {
    auto result = Json::object();
    const auto rows = document.contains("entities") ? "entities"
                      : document.contains("base")   ? "overrides"
                                                    : "members";
    for (const auto& row : document.at(rows))
        for (const char* channel : {"components", "property_overrides"}) {
            const auto values = row.find(channel);
            if (values == row.end())
                continue;
            if (const auto animator = values->find("forge.animator");
                animator != values->end() && !animator->is_null())
                if (const auto sequence = animator->find("sequence");
                    sequence != animator->end() && !sequence->is_null()) {
                    // Portable Prefab definitions retain their admitted member
                    // references; Scene/native values use EntityRef instead.
                    const bool bound = sequence->contains("prefab") || sequence->contains("member")
                                           ? bool(sequence->get<PrefabMemberRef>().member)
                                           : bool(sequence->get<EntityRef>().entity);
                    if (bound)
                        result["skeletal_sequence"] = skeletal_sequence_runtime_profile_version;
                }
            if (const auto probe = values->find("forge.reflection_probe");
                probe != values->end() && !probe->is_null())
                if (const auto capture = probe->find("runtime_capture"); capture != probe->end()) {
                    if (!capture->is_boolean())
                        throw std::runtime_error(
                            "game.export.document: Invalid Runtime capture flag");
                    if (capture->get<bool>())
                        result["reflection_capture"] = runtime_reflection_profile_version;
                }
            if (const auto fog = values->find("forge.fog_volume");
                fog != values->end() && !fog->is_null())
                result["fog_volumes"] = fog_volume_profile_version;
            if (const auto surface = values->find("forge.ui_surface_input");
                surface != values->end() && !surface->is_null()) {
                const auto geometry = surface->value("geometry", Json(0));
                if (!geometry.is_number_integer() || geometry < 0 || geometry > 1)
                    throw std::runtime_error(
                        "game.export.document: Invalid UI Surface Input geometry");
                const unsigned profile = geometry == 0 ? ui_surface_detail::quad_profile_version
                                                       : ui_surface_detail::profile_version;
                result["ui_surface_input"] =
                    std::max(result.value("ui_surface_input", 0u), profile);
            }
            if (const auto message = values->find("forge.input_message"); message != values->end())
                if (const auto trigger = message->find("trigger"); trigger != message->end()) {
                    if (!trigger->is_number_integer() || *trigger < 0 || *trigger > 2)
                        throw std::runtime_error(
                            "game.export.document: Invalid Input Message trigger");
                    if (*trigger != std::uint32_t(InputMessageTrigger::Pressed))
                        result["input_message_triggers"] = input_message_trigger_profile_version;
                }
            const auto renderer = values->find("forge.mesh_renderer");
            if (renderer == values->end())
                continue;
            const auto wind = renderer->find("wind");
            if (wind != renderer->end() && wind->value("enabled", false))
                result["mesh_wind"] = mesh_wind_profile_version;
        }
    return result;
}
bool authored_document(const AssetRecord& r) {
    return r.type == SceneAsset::type || r.type == PrefabAsset::type;
}
Json admit_document(const AssetRecord& r, std::span<const std::byte> bytes) {
    if (r.subasset || !authored_document(r))
        throw std::runtime_error("game.export.document: Unsupported authored document");
    auto doc = asset_detail::parse_bounded_json(bytes, 64 * 1024 * 1024);
    if (doc.at("asset_id").get<AssetId>() != r.id || doc.contains("_prefab_sources"))
        throw std::runtime_error("game.export.document: Identity/snapshot mismatch: " + r.id.str());
    if (r.type == SceneAsset::type) {
        Scene::validate_document(doc);
        if (doc.at("version") < 3 || doc.at("version") > 5 || r.schema_version < 3 ||
            r.schema_version > 5)
            throw std::runtime_error("game.export.document: Scene migration required");
    } else {
        PrefabDocument::validate(doc);
        if (r.schema_version < 1 || r.schema_version > 4 ||
            r.schema_version != doc.at("version").get<unsigned>())
            throw std::runtime_error("game.export.document: Unsupported prefab schema");
    }
    return doc;
}
void prepare_documents(AssetCatalog& catalog, const std::filesystem::path& root,
                       std::span<const AssetId> roots, const Json& schema,
                       RuntimePackageLimits limits, std::stop_token stop, bool packaged,
                       const RuntimeUiInspector& inspect_ui, bool allow_ui_registration) {
    std::set<AssetId> seen;
    std::vector<AssetId> pending(roots.begin(), roots.end());
    std::size_t total = 0;
    std::optional<std::map<AssetId, std::vector<AssetRecord>>> discovered;
    auto discover = [&](AssetId needed) {
        if (!discovered) {
            SourceScanOptions options;
            options.include_project_root = true;
            const auto sources = scan_asset_sources(root, options, stop);
            if (!sources.complete)
                throw std::runtime_error("game.export.discovery: Incomplete authored source scan");
            discovered.emplace();
            for (const auto& [path, source] : sources.files) {
                auto extension = path_utf8(path.extension());
                for (auto& character : extension)
                    if (character >= 'A' && character <= 'Z')
                        character = char(character + ('a' - 'A'));
                if (extension != ".json")
                    continue;
                // Malformed unrelated files do not prevent exporting a valid closure.
                // Matching reachable documents are fully admitted below.
                try {
                    const auto bytes = asset_detail::read_bytes(ProjectPaths(root).resolve(path),
                                                                64 * 1024 * 1024);
                    const auto doc = asset_detail::parse_bounded_json(bytes, 64 * 1024 * 1024);
                    const auto type =
                        doc.contains("entities") && doc.at("entities").is_array() ? SceneAsset::type
                        : doc.value("format", std::string{}) == "forge.prefab" ? PrefabAsset::type
                                                                               : "";
                    if (std::string_view(type).empty())
                        continue;
                    AssetRecord record{doc.at("asset_id").get<AssetId>(),
                                       type,
                                       path,
                                       doc.at("version").get<unsigned>(),
                                       {}};
                    (*discovered)[record.id].push_back(std::move(record));
                } catch (const std::exception&) {
                    if (stop.stop_requested())
                        throw;
                }
            }
        }
        const auto found = discovered->find(needed);
        if (found == discovered->end())
            return;
        if (found->second.size() != 1)
            throw std::runtime_error("game.export.identity: Duplicate authored AssetId " +
                                     needed.str());
        const auto& record = found->second.front();
        (void)admit_document(
            record,
            asset_detail::read_bytes(ProjectPaths(root).resolve(record.source), 64 * 1024 * 1024));
        catalog.add(record);
    };
    while (!pending.empty()) {
        if (stop.stop_requested())
            throw std::runtime_error("game.export.cancelled: Document closure cancelled");
        const auto id = pending.back();
        pending.pop_back();
        if (!seen.insert(id).second || engine_asset(id))
            continue;
        if (seen.size() > limits.assets)
            throw std::runtime_error("game.export.limit: Document closure exceeds asset limit");
        if (!packaged && !catalog.records().contains(id))
            discover(id);
        const auto found = catalog.records().find(id);
        if (found == catalog.records().end())
            throw std::runtime_error("game.export.missing: Missing dependency " + id.str());
        auto record = found->second;
        if (!packaged && record.type == UiDocumentAsset::type) {
            if (!inspect_ui)
                throw std::runtime_error(
                    "export.ui.inspection.required: Native UI inspection worker is required");
            auto candidate = prepare_ui_asset_catalog(catalog, root, inspect_ui(id, stop));
            if (!allow_ui_registration)
                for (const auto& [asset_id, unused] : candidate.records())
                    if (!catalog.records().contains(asset_id))
                        throw std::runtime_error("export.ui.registration.required: Prepare UI "
                                                 "asset identities before packaging");
            catalog = std::move(candidate);
            record = catalog.records().at(id);
        }
        if (!packaged)
            validate_runtime_declarations(root, record);
        if (authored_document(record)) {
            auto bytes = asset_detail::read_bytes(ProjectPaths(root).resolve(record.source),
                                                  64 * 1024 * 1024);
            if (bytes.size() > 256 * 1024 * 1024 - total)
                throw std::runtime_error("game.export.limit: Documents exceed 256 MiB");
            total += bytes.size();
            const std::array documents{
                AssetReferenceDocument{record.source, admit_document(record, bytes)}};
            const auto refs = collect_asset_references(schema, documents);
            if (!refs.uninspected.empty())
                throw std::runtime_error("game.export.opaque: Dependency coverage is unknown: " +
                                         refs.uninspected.front());
            // Preserve explicit native/dynamic dependencies; refresh only those
            // owned by this authoritative reflected-document adapter.
            if (!record.dependencies.empty() && record.dependency_edges.empty())
                throw std::runtime_error("game.export.untyped: Repair legacy dependency edges: " +
                                         id.str());
            std::erase_if(record.dependency_edges,
                          [](const auto& edge) { return edge.role.starts_with("document:"); });
            for (const auto& hit : refs.references) {
                if (hit.target == id && hit.expected_type != record.type)
                    throw std::runtime_error(
                        "game.export.type: Self reference has wrong asset type: " + id.str());
                if (hit.target != id)
                    record.dependency_edges.push_back({hit.target,
                                                       hit.expected_type,
                                                       AssetDependencyKind::Runtime,
                                                       "document:" + hit.property,
                                                       {}});
            }
            std::sort(record.dependency_edges.begin(), record.dependency_edges.end());
            record.dependency_edges.erase(
                std::unique(record.dependency_edges.begin(), record.dependency_edges.end()),
                record.dependency_edges.end());
            std::set<AssetId> targets;
            for (const auto& edge : record.dependency_edges)
                targets.insert(edge.target);
            record.dependencies.assign(targets.begin(), targets.end());
            record.metadata["forge.runtime_document"] = {
                {"version", 1},
                {"sha256", asset_detail::content_digest(bytes)},
                {"schema_digest", asset_build_digest(schema)}};
            const auto features = document_runtime_features(documents.front().document);
            if (!features.empty())
                record.metadata["forge.runtime_document"]["runtime_features"] = features;
            catalog.replace(record);
        }
        if (record.subasset)
            pending.push_back(record.subasset->owner);
        for (const auto& edge : catalog.dependency_graph().dependencies(id))
            if (edge.kind == AssetDependencyKind::Runtime && edge.role != "ui.observed")
                pending.push_back(edge.target);
    }
}
} // namespace forge::package_detail
