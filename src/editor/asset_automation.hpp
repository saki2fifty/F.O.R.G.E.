#pragma once
#include "asset_file_automation.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "content_imports.hpp"
#include "scene_asset_drop.hpp"
#include "source_import.hpp"
#include <forge/engine_assets.hpp>

namespace forge {
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
inline std::vector<LiveAuthoringMethod>
asset_automation_methods(Scene& scene, EditorFiles& files, ContentImports& imports,
                         SourceImport& source, SceneAssetDrop& placement,
                         ContentFiles& content_files,
                         const ui::AutomationPermissions& permissions) {
    auto methods =
        asset_file_automation_methods(files.document, content_files, imports, permissions.assets);
    auto catalog = [&files, &imports] {
        files.document.check_ownership();
        auto current = imports.catalog();
        if (!current || current->project_root() != files.document.project())
            throw AuthoringError("not_ready", "Wait for the current project's asset catalog");
        return current;
    };
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
        "asset.jobs",
        "Read existing background reimport, source batch and placement progress/results.",
        "asset.read", automation_object(), false,
        [catalog, &files, &imports, &source, &placement, &content_files](const Json&) {
            (void)catalog();
            return Json{{"reimports", imports.jobs()},
                        {"source_import", source.project() == files.document.project()
                                              ? source.operation()
                                              : Json{{"state", "idle"}}},
                        {"placement", placement.project() == files.document.project()
                                          ? placement.operation()
                                          : Json{{"state", "idle"}}},
                        {"files", content_files.project() == files.document.project()
                                      ? content_files.automation_status()
                                      : Json{{"state", "idle"}}}};
        }));
    methods.push_back(automation_method(
        "asset.import.review",
        "Read one page of the prepared source-copy manifest and review digest.", "asset.read",
        automation_object(
            {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
        false, [&files, &source](const Json& request) {
            if (source.project() != files.document.project())
                return Json{{"state", "idle"}};
            return source.operation(request.value("offset", std::size_t{}),
                                    request.value("limit", std::size_t(128)));
        }));
    if (!permissions.assets)
        return methods;
    methods.push_back(automation_method(
        "asset.reimport",
        "Queue registered assets through the existing dependency-aware reimport owner.",
        "asset.write",
        automation_object({{"assets", automation_array(automation_uuid(), 256, 1)}}, {"assets"}),
        true, [catalog, &imports](const Json& request) {
            (void)catalog();
            std::vector<AssetId> assets;
            for (const auto& id : request.at("assets"))
                assets.push_back(AssetId::parse(id.get<std::string>()));
            imports.reimport(assets);
            return Json{{"queued", request.at("assets")}};
        }));
    methods.push_back(automation_method(
        "asset.import.prepare",
        "Prepare a reviewed raw-source copy/import batch. Paths are relative to the project or "
        "explicitly granted import root; no files change yet.",
        "asset.write",
        automation_object({{"sources", automation_array(automation_text(2047, 1), 256, 1)},
                           {"root", {{"type", "string"}, {"enum", {"project", "import"}}}},
                           {"destination", automation_text(2047, 1)}},
                          {"sources", "destination"}),
        true, [&files, &source, permissions](const Json& request) {
            const bool external = request.value("root", std::string("project")) == "import";
            const auto root = external ? permissions.import_root : files.document.project();
            if (root.empty() || !std::filesystem::is_directory(root))
                throw AuthoringError("capability_denied", "No readable import root was granted");
            std::vector<std::filesystem::path> paths;
            const ProjectPaths scope(root);
            for (const auto& value : request.at("sources"))
                paths.push_back(scope.resolve(automation_relative_path(value.get<std::string>())));
            source.automation_prepare(
                files.document, std::move(paths),
                automation_relative_path(request.at("destination").get<std::string>()));
            return source.operation();
        }));
    methods.push_back(automation_method(
        "asset.import.commit",
        "Commit this exact reviewed copy plan, optionally importing with defaults. Publication "
        "remains per asset; observe progress/results.",
        "asset.write",
        automation_object({{"batch", automation_integer(1)},
                           {"review", automation_digest()},
                           {"import", {{"type", "boolean"}}}},
                          {"batch", "review"}),
        true,
        [&files, &source](const Json& request) {
            if (source.project() != files.document.project())
                throw AuthoringError("wrong_job", "Import batch belongs to another project");
            source.automation_commit(request.at("batch").get<std::uint64_t>(),
                                     request.at("review").get<std::string>(),
                                     request.value("import", true));
            return source.operation();
        },
        true));
    for (bool close : {false, true})
        methods.push_back(automation_method(
            close ? "asset.import.close" : "asset.import.cancel",
            close ? "Close a settled import review; completed copies/publications remain."
                  : "Cancel pending work in this exact source batch; completed copies/publications "
                    "remain.",
            "asset.write", automation_object({{"batch", automation_integer(1)}}, {"batch"}), true,
            [&files, &source, close](const Json& request) {
                if (source.project() != files.document.project())
                    throw AuthoringError("wrong_job", "Import batch belongs to another project");
                if (close)
                    source.automation_close(request.at("batch").get<std::uint64_t>());
                else
                    source.automation_cancel(request.at("batch").get<std::uint64_t>());
                return source.operation();
            },
            true));
    methods.push_back(automation_method(
        "asset.place",
        "Queue an undoable Model, Mesh, Tile Map or Prefab placement in this scene. Observe "
        "asset.jobs for its actual completion.",
        "asset.place",
        automation_object({{"asset", automation_uuid()},
                           {"position", automation_array({{"type", "number"}}, 3, 3)},
                           {"name", automation_text(256, 1)},
                           {"source_scene", automation_integer(0, UINT32_MAX)},
                           {"clip", automation_uuid()},
                           {"material_variant", automation_uuid()}},
                          {"asset", "position"}),
        true, [catalog, &scene, &files, &placement](const Json& request) {
            const auto current = catalog();
            const auto id = AssetId::parse(request.at("asset").get<std::string>());
            AssetRecord asset;
            if (const auto* builtin = engine_asset(id))
                asset = {id, builtin->type, {}};
            else {
                const auto found = current->records().find(id);
                if (found == current->records().end())
                    throw AuthoringError("not_found", "Asset is not registered in this project");
                asset = found->second;
            }
            if (asset.type == "scene" || !SceneAssetDrop::supported(asset.type))
                throw AuthoringError(
                    "unsupported_asset",
                    "Place a Model, Mesh, Tile Map or Prefab; use document.open for scenes");
            asset_detail::ModelPlacementOptions options;
            options.name = request.value("name", asset_display(asset));
            if (request.contains("source_scene"))
                options.source_scene = request.at("source_scene").get<std::uint32_t>();
            if (request.contains("clip"))
                options.clip.id = AssetId::parse(request.at("clip").get<std::string>());
            if (request.contains("material_variant"))
                options.material_variant.id =
                    AssetId::parse(request.at("material_variant").get<std::string>());
            if (asset.type != "model" &&
                (request.contains("source_scene") || request.contains("clip") ||
                 request.contains("material_variant")))
                throw AuthoringError("invalid_arguments",
                                     "Model scene/clip/variant options require a Model asset");
            const auto values = request.at("position").get<std::array<double, 3>>();
            placement.queue(asset, scene, files.document, {values[0], values[1], values[2]},
                            options);
            return placement.operation();
        }));
    methods.push_back(automation_method(
        "asset.place.cancel",
        "Cancel this exact pending placement; committed scene changes remain undoable.",
        "asset.place", automation_object({{"job", automation_integer(1)}}, {"job"}), true,
        [&files, &placement](const Json& request) {
            if (placement.project() != files.document.project())
                throw AuthoringError("wrong_job", "Placement belongs to another project");
            placement.cancel(request.at("job").get<std::uint64_t>());
            return placement.operation();
        },
        true));
    return methods;
}
} // namespace forge
