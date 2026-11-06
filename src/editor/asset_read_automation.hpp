#pragma once
#include "../mesh_pick.hpp"
#include "../model_render_resource.hpp"
#include "automation_schema.hpp"
#include "content_imports.hpp"
#include "property_drawer.hpp"
#include <forge/engine_assets.hpp>

namespace forge {
inline std::shared_ptr<const AssetCatalog> automation_asset_catalog(SceneDocument& document,
                                                                    ContentImports& imports) {
    document.check_ownership();
    auto current = imports.catalog();
    if (!current || current->project_root() != document.project())
        throw AuthoringError("not_ready", "Wait for the current project's asset catalog");
    return current;
}
inline Json automation_asset_record(const AssetRecord& asset, bool detail) {
    Json result = {{"asset", asset.id.str()},
                   {"type", asset.type},
                   {"name", asset_display(asset)},
                   {"source", path_utf8(asset.source)},
                   {"schema_version", asset.schema_version}};
    if (asset.subasset)
        result["subasset"] = {{"owner", asset.subasset->owner.str()},
                              {"key", asset.subasset->key},
                              {"removed", asset.subasset->removed}};
    if (detail) {
        result["metadata"] = asset.metadata;
        result["dependencies"] = Json::array();
        for (const auto id : asset.dependencies)
            result["dependencies"].push_back(id.str());
    }
    return result;
}
inline Json automation_engine_asset(const EngineAsset& asset) {
    return {{"asset", asset.id.str()},
            {"type", asset.type},
            {"name", asset.name},
            {"engine_owned", true},
            {"revision", engine_asset_revision(asset.id)}};
}
inline std::vector<LiveAuthoringMethod> asset_read_automation_methods(SceneDocument& document,
                                                                      ContentImports& imports) {
    std::vector<LiveAuthoringMethod> methods;
    auto catalog = [&document, &imports] { return automation_asset_catalog(document, imports); };
    methods.push_back(automation_method(
        "asset.search", "Page current project and immutable engine assets by name, source or type.",
        "asset.read",
        automation_object({{"text", automation_text()},
                           {"type", automation_text(128)},
                           {"offset", automation_integer()},
                           {"limit", automation_integer(1, 128)},
                           {"include_engine", {{"type", "boolean"}}}}),
        false, [catalog](const Json& request) {
            const auto current = catalog();
            const auto text = request.value("text", std::string{});
            const auto type = request.value("type", std::string{});
            const auto offset = request.value("offset", std::size_t{});
            const auto limit = request.value("limit", std::size_t(128));
            std::size_t total = 0;
            Json records = Json::array();
            auto collect = [&](Json record) {
                if ((!type.empty() && record.at("type") != type) ||
                    (!text.empty() &&
                     record.at("name").get<std::string>().find(text) == std::string::npos &&
                     record.value("source", std::string{}).find(text) == std::string::npos &&
                     record.at("asset").get<std::string>().find(text) == std::string::npos))
                    return;
                if (total >= offset && records.size() < limit)
                    records.push_back(std::move(record));
                ++total;
            };
            if (request.value("include_engine", true))
                for (const auto& asset : engine_assets())
                    collect(automation_engine_asset(asset));
            for (const auto& [id, asset] : current->records()) {
                (void)id;
                collect(automation_asset_record(asset, false));
            }
            return Json{{"assets", records}, {"offset", offset}, {"total", total}};
        }));
    methods.push_back(automation_method(
        "asset.read", "Read a catalog record, dependencies and current resolution diagnostics.",
        "asset.read", automation_object({{"asset", automation_uuid()}}, {"asset"}), false,
        [catalog](const Json& request) {
            const auto current = catalog();
            const auto id = AssetId::parse(request.at("asset").get<std::string>());
            if (const auto* builtin = engine_asset(id))
                return automation_engine_asset(*builtin);
            const auto found = current->records().find(id);
            if (found == current->records().end())
                throw AuthoringError("not_found", "Asset is not registered in this project");
            auto result = automation_asset_record(found->second, true);
            const auto resolution = current->resolve(id, found->second.type);
            result["state"] = asset_state_name(resolution.state);
            result["diagnostic"] = resolution.diagnostic;
            return result;
        }));
    methods.push_back(automation_method(
        "asset.mesh.triangles",
        "Read a bounded page of published Mesh object-space triangles, indices and UV0 for "
        "surface painting. Engine, root Mesh and model members share immutable selection. "
        "These are source-pose coordinates, without Scene transforms, animation or visibility.",
        "asset.read",
        automation_object({{"mesh", automation_uuid()},
                           {"revision", automation_text(64, 64)},
                           {"lod", automation_integer(0, UINT32_MAX)},
                           {"part", automation_integer(0, UINT32_MAX)},
                           {"offset", automation_integer()},
                           {"limit", automation_integer(1, 128)}},
                          {"mesh"}),
        false, [catalog, &document](const Json& request) {
            const auto current = catalog();
            const AssetRef<MeshAsset> ref{request.at("mesh").get<AssetId>()};
            const auto revision = asset_detail::selected_mesh_revision(*current, ref);
            if (request.contains("revision") && request.at("revision") != revision)
                throw AuthoringError("stale_asset",
                                     "Mesh publication changed; refresh its revision");
            MeshLimits limits;
            limits.bytes = 32 * 1024 * 1024;
            const auto selected =
                asset_detail::load_mesh_selection(document.project(), *current, ref, limits);
            const auto lod = request.value("lod", 0u), part = request.value("part", 0u);
            const auto& data = selected.value->mesh;
            if (lod >= data.lods.size() || part >= data.lods[lod].parts.size())
                throw AuthoringError("invalid_argument",
                                     "Mesh LOD or part is outside the published geometry");
            const auto& geometry = data.lods[lod].parts[part];
            const auto* positions = geometry.find("POSITION");
            const auto* uv = geometry.find("TEXCOORD_0");
            if (geometry.topology != MeshTopology::Triangles || !positions ||
                positions->components != 3 ||
                !std::holds_alternative<std::vector<float>>(positions->values))
                throw AuthoringError("unsupported_geometry",
                                     "Choose published triangles with float3 positions");
            const auto& vertices = std::get<std::vector<float>>(positions->values);
            const auto* coordinates =
                uv && uv->components == 2 ? std::get_if<std::vector<float>>(&uv->values) : nullptr;
            const auto count =
                (geometry.indices.empty() ? geometry.vertices : geometry.indices.size()) / 3;
            const auto offset = request.value("offset", std::size_t{}),
                       limit = request.value("limit", std::size_t(128));
            Json triangles = Json::array();
            for (std::size_t i = offset; i < count && i - offset < limit; ++i) {
                const auto indices = mesh_triangle_indices(geometry, i);
                Json points = Json::array(), uvs = Json::array();
                for (const auto index : indices) {
                    points.push_back({vertices.at(index * 3ull), vertices.at(index * 3ull + 1),
                                      vertices.at(index * 3ull + 2)});
                    if (coordinates)
                        uvs.push_back(
                            {coordinates->at(index * 2ull), coordinates->at(index * 2ull + 1)});
                }
                triangles.push_back({{"primitive", i},
                                     {"indices", indices},
                                     {"positions", std::move(points)},
                                     {"uv0", std::move(uvs)}});
            }
            return Json{{"mesh", ref.id},
                        {"revision", revision},
                        {"lod", lod},
                        {"part", part},
                        {"lod_count", data.lods.size()},
                        {"part_count", data.lods[lod].parts.size()},
                        {"triangle_count", count},
                        {"offset", offset},
                        {"triangles", std::move(triangles)}};
        }));
    return methods;
}
} // namespace forge
