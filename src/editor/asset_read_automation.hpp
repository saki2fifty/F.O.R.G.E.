#pragma once
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
    return methods;
}
} // namespace forge
