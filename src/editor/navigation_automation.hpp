#pragma once
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "navigation_tools.hpp"
namespace forge {
struct NavigationAutomation {
    static Json state(NavigationTools& owner, Scene& scene, EditorFiles& files) {
        files.document.check_ownership();
        if (owner.loaded_project_ != files.document.project() ||
            owner.loaded_scene_ != scene.asset_id())
            return {{"busy", false}, {"started", false}};
        return {{"started", true},
                {"busy", owner.pending()},
                {"operation", owner.operation_},
                {"settings", owner.settings_},
                {"asset", owner.selected_ ? Json(owner.selected_) : Json()},
                {"status", owner.status_},
                {"error", owner.error_}};
    }
    static std::vector<LiveAuthoringMethod> methods(NavigationTools& owner, Scene& scene,
                                                    EditorFiles& files,
                                                    const ui::AutomationPermissions& permissions,
                                                    std::function<std::string()> unavailable) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "navigation.status",
            "Read the existing navigation bake owner for this Scene. Its "
            "immutable navmesh asset publication is separate from Scene Undo.",
            "assets.read", automation_object(), false,
            [&owner, &scene, &files](const Json&) { return state(owner, scene, files); }));
        if (!permissions.assets)
            return result;
        Json values;
        for (const auto* name : {"radius", "height", "climb", "slope", "cell_size", "cell_height"})
            values[name] = {{"type", "number"}};
        result.push_back(automation_method(
            "navigation.build",
            "Bake marked static geometry through the existing native worker "
            "and stale-geometry publication check. Settings use the native "
            "agent/voxel profile; poll navigation.status and assign the asset "
            "with ordinary component/property tools.",
            "assets.write",
            automation_object(
                {{"settings", automation_object(values, {"radius", "height", "climb", "slope",
                                                         "cell_size", "cell_height"})}},
                {"settings"}),
            true, [&owner, &scene, &files, unavailable](const Json& request) {
                files.document.check_ownership();
                const auto reason = unavailable ? unavailable() : "";
                if (!reason.empty())
                    throw AuthoringError("navigation_busy", reason);
                owner.build(scene, files.document,
                            request.at("settings").get<NavigationSettings>());
                return state(owner, scene, files);
            }));
        result.push_back(automation_method(
            "navigation.cancel",
            "Cancel the exact unpublished navigation candidate through its "
            "current owner. The previous asset remains available.",
            "assets.write", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            true,
            [&owner, &scene, &files](const Json& request) {
                files.document.check_ownership();
                if (!owner.pending() || owner.job_project_ != files.document.project() ||
                    owner.job_scene_ != scene.asset_id() ||
                    request.at("operation") != owner.operation_)
                    throw AuthoringError("wrong_job", "Choose this Scene's exact navigation bake");
                owner.cancel_.request_stop();
                return state(owner, scene, files);
            },
            true));
        return result;
    }
};
} // namespace forge
