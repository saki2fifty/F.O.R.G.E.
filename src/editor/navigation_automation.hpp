#pragma once
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "navigation_tools.hpp"
namespace forge {
struct NavigationAutomation {
    static Json state(NavigationTools& owner, Scene& scene, EditorFiles& files) {
        files.document.check_ownership();
        const auto profiles = navigation_profiles(
            AssetCatalog::open_project(files.document.project()), scene.asset_id());
        if (owner.loaded_project_ != files.document.project() ||
            owner.loaded_scene_ != scene.asset_id())
            return {{"busy", false},
                    {"started", false},
                    {"profiles", profiles},
                    {"overlay_visible", owner.overlay_visible()}};
        return {{"started", true},
                {"busy", owner.pending()},
                {"operation", owner.operation_},
                {"settings", owner.settings_},
                {"profiles", profiles},
                {"asset", owner.selected_ ? Json(owner.selected_) : Json()},
                {"link_count", owner.metadata_.is_object()
                                   ? owner.metadata_.value("links", Json::array()).size()
                                   : 0},
                {"status", owner.status_},
                {"overlay_visible", owner.overlay_visible()},
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
        result.push_back(automation_method(
            "navigation.select_profile",
            "Select an existing named navigation asset/settings view or begin a new profile "
            "draft. This does not bake, publish or change Scene history. Names are "
            "case-sensitive ASCII tokens; default preserves legacy asset identity.",
            "assets.write",
            automation_object({{"profile",
                                {{"type", "string"},
                                 {"minLength", 1},
                                 {"maxLength", 64},
                                 {"pattern", "^[A-Za-z][A-Za-z0-9_-]*$"}}}},
                              {"profile"}),
            true, [&owner, &scene, &files, unavailable](const Json& request) {
                files.document.check_ownership();
                const auto reason = unavailable ? unavailable() : "";
                if (!reason.empty())
                    throw AuthoringError("navigation_busy", reason);
                owner.select_profile(scene, files.document,
                                     AssetCatalog::open_project(files.document.project()),
                                     request.at("profile").get<std::string>());
                return state(owner, scene, files);
            }));
        result.push_back(automation_method(
            "navigation.overlay",
            "Show or hide the existing navigation overlay, including saved mesh triangles, "
            "authored link direction guides, runtime paths and the selected agent's requested "
            "destination. This changes only "
            "the current editor view, without Scene history, asset publication or file writes.",
            "assets.write", automation_object({{"visible", {{"type", "boolean"}}}}, {"visible"}),
            true, [&owner, &scene, &files](const Json& request) {
                files.document.check_ownership();
                owner.show_overlay(request.at("visible").get<bool>());
                return state(owner, scene, files);
            }));
        Json values;
        for (const auto* name : {"radius", "height", "climb", "slope", "cell_size", "cell_height"})
            values[name] = {{"type", "number"}};
        values["dynamic_obstacles"] = {
            {"type", "boolean"},
            {"description",
             "Opt into tiled navigation for moving Navigation Obstacle cylinders/circles."}};
        values["plane"] = {{"type", "integer"},
                           {"enum", {0, 1}},
                           {"description", "0: spatial 3D (default); 1: top-down XY regions."}};
        values["profile"] = {{"type", "string"},
                             {"minLength", 1},
                             {"maxLength", 64},
                             {"pattern", "^[A-Za-z][A-Za-z0-9_-]*$"},
                             {"description", "Case-sensitive Scene-local build name; omitted "
                                             "means default and retains the existing asset ID."}};
        result.push_back(automation_method(
            "navigation.build",
            "Bake marked static 3D geometry or selected XY region sources through the existing "
            "native "
            "worker "
            "and stale-geometry publication check. Settings use the native "
            "agent/voxel settings, optional named profile and dynamic obstacle tiles. Enabled "
            "Navigation Link components contribute their local Start/End and direction only to "
            "the matching plane/profile; Agents traverse admitted spans at their speed. Poll "
            "navigation.status and "
            "assign the asset "
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
