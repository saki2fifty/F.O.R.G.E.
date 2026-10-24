#pragma once
#include "../asset_bytes.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "runtime_dependencies.hpp"
namespace forge {
struct DependencyAutomation {
    static Json state(ui::RuntimeDependenciesEditor& owner, EditorFiles& files) {
        files.document.check_ownership();
        if (owner.project_ != files.document.project() || !owner.owner_)
            return {{"open", false}, {"busy", false}};
        Json edges = owner.edges_;
        const auto text =
            Json{{"session", owner.session_}, {"edges", edges}, {"operation", owner.operation_}}
                .dump();
        const auto digest =
            asset_detail::content_digest(std::as_bytes(std::span(text.data(), text.size())));
        return {{"open", true},
                {"asset", owner.owner_},
                {"state", digest},
                {"session", owner.session_},
                {"edges", std::move(edges)},
                {"dirty", owner.dirty()},
                {"busy", owner.busy()},
                {"operation", owner.operation_},
                {"status", owner.status_},
                {"error", owner.error_}};
    }
    static std::vector<LiveAuthoringMethod>
    methods(ui::RuntimeDependenciesEditor& owner, EditorFiles& files,
            const ui::AutomationPermissions& permissions,
            std::function<std::shared_ptr<const AssetCatalog>()> catalog,
            std::function<void(AssetId)> select, std::function<std::string()> unavailable) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(
            automation_method("dependencies.read",
                              "Read the actual selected asset's runtime-dependency draft, "
                              "review digest and native save task. AssetCatalog remains the "
                              "only dependency graph; Scene Undo does not edit declarations.",
                              "assets.read", automation_object(), false,
                              [&owner, &files](const Json&) { return state(owner, files); }));
        if (!permissions.assets)
            return result;
        const auto ready = [&files, unavailable] {
            files.document.check_ownership();
            const auto reason = unavailable ? unavailable() : "";
            if (!reason.empty())
                throw AuthoringError("dependencies_busy", reason);
        };
        const auto current_catalog = [&files, catalog] {
            auto current = catalog();
            if (!current || current->project_root() != files.document.project())
                throw AuthoringError("not_ready", "Wait for the current Content catalog");
            return current;
        };
        result.push_back(automation_method(
            "dependencies.open",
            "Select an authored asset in Content and load its existing "
            "runtime-dependency review. Settle a dirty prior review first.",
            "assets.write", automation_object({{"asset", automation_uuid()}}, {"asset"}), true,
            [&owner, &files, ready, select, current_catalog](const Json& request) {
                ready();
                if (owner.busy() || owner.dirty())
                    throw AuthoringError("dependencies_busy",
                                         "Save or discard the previous dependency review");
                const auto current = current_catalog();
                const auto id = request.at("asset").get<AssetId>();
                const auto found = current->records().find(id);
                if (found == current->records().end() || found->second.subasset)
                    throw AuthoringError("invalid_asset",
                                         "Select an authored dependency owner asset");
                owner.project_ = files.document.project();
                owner.load(*current, id);
                select(id);
                return state(owner, files);
            }));
        for (const auto* action : {"edit", "save", "discard"}) {
            Json values{{"state", automation_digest()}}, keys{"state"};
            if (std::string_view(action) == "edit") {
                values["dependencies"] =
                    automation_array(automation_object({{"asset", automation_uuid()},
                                                        {"type", automation_text(128, 1)},
                                                        {"reason", automation_text(100, 1)},
                                                        {"module", automation_text(128, 1)}},
                                                       {"asset", "type", "reason"}),
                                     4096);
                keys.push_back("dependencies");
            }
            result.push_back(automation_method(
                std::string("dependencies.") + action,
                std::string(action) +
                    " the exact native dependency review. Edit supplies the "
                    "complete finite declared set; Save validates types, optional UI discovery, "
                    "reviewed source revisions and the persisted catalog baseline in its "
                    "existing worker. Poll dependencies.read; no automatic retry or merge.",
                "assets.write", automation_object(values, keys), true,
                [&owner, &files, ready, current_catalog, action](const Json& request) {
                    ready();
                    const auto observed = state(owner, files);
                    if (!observed.at("open").get<bool>() ||
                        observed.at("state") != request.at("state"))
                        throw AuthoringError("stale_dependencies",
                                             "Read the current dependency review first");
                    if (owner.busy())
                        throw AuthoringError("dependencies_busy",
                                             "Wait for native dependency validation/publication");
                    const auto current = current_catalog();
                    const auto operation = std::string_view(action);
                    if (operation == "edit") {
                        std::vector<AssetDependency> candidate;
                        for (const auto& row : request.at("dependencies")) {
                            auto reason = row.at("reason").get<std::string>();
                            if (reason.starts_with("module/"))
                                throw AuthoringError(
                                    "invalid_reason",
                                    "Use the separate module field for module ownership");
                            std::string role = "declared:";
                            if (row.contains("module"))
                                role += "module/" + row.at("module").get<std::string>() + "/";
                            role += reason;
                            const auto target = row.at("asset").get<AssetId>();
                            const auto type = row.at("type").get<std::string>();
                            if (role.size() > 128 || target == owner.owner_ ||
                                current->resolve(target, type).state != AssetState::Available)
                                throw AuthoringError("invalid_dependency",
                                                     "Choose an available typed asset, a different "
                                                     "owner and a bounded reason");
                            candidate.push_back(
                                {target, type, AssetDependencyKind::Runtime, role, {}});
                        }
                        owner.edges_ = std::move(candidate);
                        owner.dirty_ = true;
                    } else if (operation == "save")
                        owner.save(files.document, *current);
                    else
                        owner.load(*current, owner.owner_);
                    return state(owner, files);
                }));
        }
        return result;
    }
};
} // namespace forge
