#pragma once
#include "../foliage_collision.hpp"
#include "../terrain_foliage_authoring.hpp"
#include "automation_schema.hpp"
#include "document.hpp"
namespace forge {
inline std::vector<LiveAuthoringMethod>
foliage_automation_methods(Scene& scene, SceneDocument& project,
                           FoliageCollisionPreparation* clearance = nullptr) {
    std::vector<LiveAuthoringMethod> methods;
    Json scatter_schema;
    for (const auto& command : authoring_commands())
        if (command.at("id") == "mesh.scatter")
            scatter_schema = command.at("input_schema");
    scatter_schema["properties"].erase("world_heights");
    scatter_schema["properties"].erase("placement_mask");
    scatter_schema["properties"].erase("world_normals");
    methods.push_back(automation_method(
        "foliage.terrain.prepare",
        "Read a published upright heightfield and prepare static foliage heights. Returns "
        "mesh.scatter arguments and scene_revision; apply them with forge_mesh_scatter at that "
        "revision. No Scene or file changes. On-disk source changes and uncovered origins "
        "reject preparation. Terrain yaw/scaling are supported; tilt/inversion are not. "
        "Optional minimum_slope/maximum_slope select world geometric triangle slope in degrees "
        "(0..90, inclusive; default0..90). Count is a bounded candidate pool; returned "
        "placement_mask/retained_count identify the subset, without resampling. Zero retained "
        "candidates rejects. Optional align_to_slope supplies world_normals to the same scatter "
        "owner: tilt the placement frame to the geometric normal; yaw/template rotation remain "
        "relative. Default false retains world-up orientation. No collision tests or live link.",
        "asset.read",
        automation_object(
            {{"terrain", automation_uuid()},
             {"arguments", scatter_schema},
             {"height_offset", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
             {"minimum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
             {"maximum_slope", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}}},
             {"align_to_slope", {{"type", "boolean"}}}},
            {"terrain", "arguments"}),
        false, [&scene, &project](const Json& request) {
            return prepare_terrain_foliage(
                *project.writer_guard(), scene, request.at("arguments"), request.at("terrain"),
                request.value("height_offset", 0.),
                {request.value("minimum_slope", 0.), request.value("maximum_slope", 90.)},
                request.value("align_to_slope", false));
        }));
    if (!clearance)
        return methods;
    Json complete_scatter;
    for (const auto& command : authoring_commands())
        if (command.at("id") == "mesh.scatter")
            complete_scatter = command.at("input_schema");
    const Json coordinate{{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}};
    methods.push_back(automation_method(
        "foliage.collision.start",
        "Prepare read-only world-sphere-v1 clearance against current authored native 3D colliders. "
        "Poll foliage.collision.read with the returned operation. Supply ordinary scatter "
        "arguments "
        "or the result of foliage.terrain.prepare; heights, normals and original-order masks are "
        "preserved. Radius0.001..100m; world offset defaults(0,0.5,0)m. Layer mask defaults all; "
        "sensors default excluded. The template and optional surface are ignored automatically, "
        "plus at most14 distinct ignored entity IDs. At most512 candidates,16 queries per poll, "
        "30-second deadline. Exact retries reuse the operation; cancel a different pending request "
        "first. No simulation or Scene/file changes. Ready arguments use forge_mesh_scatter at the "
        "returned scene_revision. Recheck before applying after published assets/settings change. "
        "This sphere tests existing colliders, not full mesh bounds or candidate-to-candidate "
        "overlap.",
        "asset.read",
        automation_object(
            {{"arguments", complete_scatter},
             {"radius", {{"type", "number"}, {"minimum", .001}, {"maximum", 100}}},
             {"offset", automation_object({{"x", coordinate}, {"y", coordinate}, {"z", coordinate}},
                                          {"x", "y", "z"})},
             {"mask", automation_integer(0, UINT32_MAX)},
             {"include_sensors", {{"type", "boolean"}}},
             {"surface", automation_uuid()},
             {"ignored", automation_array(automation_uuid(), 14)}},
            {"arguments"}),
        false, [&scene, &project, clearance](const Json& request) {
            FoliageCollisionSettings settings;
            settings.radius = request.value("radius", settings.radius);
            if (request.contains("offset"))
                settings.offset = {request.at("offset").at("x"), request.at("offset").at("y"),
                                   request.at("offset").at("z")};
            settings.mask = request.value("mask", settings.mask);
            settings.include_sensors = request.value("include_sensors", false);
            if (request.contains("surface"))
                settings.surface = request.at("surface").get<EntityId>();
            if (request.contains("ignored"))
                settings.ignored = request.at("ignored").get<std::vector<EntityId>>();
            return clearance->start(project.writer_guard(), scene, request.at("arguments"),
                                    settings);
        }));
    for (const bool cancel : {false, true})
        methods.push_back(automation_method(
            cancel ? "foliage.collision.cancel" : "foliage.collision.read",
            cancel ? "Cancel this exact transient foliage clearance; authored objects remain "
                     "unchanged."
                   : "Poll/read this exact foliage clearance operation. Ready results contain "
                     "copied scatter arguments and the source Scene revision; "
                     "failures/cancellation have no result.",
            "asset.read", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            false, [&scene, &project, clearance, cancel](const Json& request) {
                project.check_ownership();
                if (clearance->state().value("operation", "") != request.at("operation"))
                    throw AuthoringError("wrong_operation",
                                         "Foliage clearance operation does not match");
                if (cancel)
                    clearance->cancel();
                return clearance->read(scene, project.project());
            }));
    return methods;
}
} // namespace forge
