#pragma once
#include "../terrain_foliage_authoring.hpp"
#include "automation_schema.hpp"
#include "document.hpp"
namespace forge {
inline std::vector<LiveAuthoringMethod> foliage_automation_methods(Scene& scene,
                                                                   SceneDocument& project) {
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
    return methods;
}
} // namespace forge
