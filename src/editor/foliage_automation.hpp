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
    methods.push_back(automation_method(
        "foliage.terrain.prepare",
        "Read a published upright heightfield and prepare static foliage heights. Returns "
        "mesh.scatter arguments and scene_revision; apply them with forge_mesh_scatter at that "
        "revision. No Scene or file changes. On-disk source changes and uncovered origins "
        "reject preparation. Terrain yaw/scaling are supported; tilt/inversion are not.",
        "asset.read",
        automation_object(
            {{"terrain", automation_uuid()},
             {"arguments", scatter_schema},
             {"height_offset", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}}},
            {"terrain", "arguments"}),
        false, [&scene, &project](const Json& request) {
            return prepare_terrain_foliage(*project.writer_guard(), scene, request.at("arguments"),
                                           request.at("terrain"),
                                           request.value("height_offset", 0.));
        }));
    return methods;
}
} // namespace forge
