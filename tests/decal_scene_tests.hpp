#pragma once
#include "editor/document.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/material_source.hpp>
#include <forge/render_scene.hpp>
#include <fstream>
namespace forge::test_decal_scene {
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Prepared {
    std::filesystem::path root, scratch;
    AssetId scene;
    std::size_t prior_records{};
    bool created{};
};
inline Prepared prepare(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                        bool surface_channels = false) {
    Prepared result;
    result.scratch = scratch / AssetId::generate().str();
    result.root = result.scratch / "Project";
    std::filesystem::create_directories(result.scratch);
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    const auto destination =
        result.root / (surface_channels ? "Scenes/115-projected-surface-channels.scene.json"
                                        : "Scenes/113-projected-color-decals.scene.json");
    result.created = !std::filesystem::exists(destination);
    const auto prior = AssetCatalog::open_project(result.root).document().at("assets");
    result.prior_records = prior.size();
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(result.root, true);
    const auto settings = project.settings().document();
    if (result.created) {
        project.open_scene(result.root / (surface_channels
                                              ? "Scenes/113-projected-color-decals.scene.json"
                                              : "Scenes/112-area-lights.scene.json"));
        project.save_as(destination);
        const auto set = [&](const std::string& id, const char* component, const Json& values) {
            authoring_command(scene, "property.set_many",
                              {{"entity", id}, {"component", component}, {"values", values}});
        };
        const auto pose = [&](const std::string& id, Double3 position, LocalRotation rotation,
                              LocalScale scale = {}) {
            authoring_command(
                scene, "transform.local",
                {{"entity", id},
                 {"translation", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}},
                 {"rotation",
                  {{"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}}},
                 {"scale", {{"x", scale.x}, {"y", scale.y}, {"z", scale.z}}}});
        };
        const auto parameters = [](std::array<float, 4> color) {
            return Json::array(
                {{{"slot", "surface"},
                  {"key", "baseColorFactor"},
                  {"parameter",
                   {{"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", color}}}},
                 {{"slot", "surface"},
                  {"key", "metallicFactor"},
                  {"parameter",
                   {{"type", unsigned(MaterialParameterType::Scalar)}, {"value", {0, 0, 0, 0}}}}},
                 {{"slot", "surface"},
                  {"key", "roughnessFactor"},
                  {"parameter",
                   {{"type", unsigned(MaterialParameterType::Scalar)},
                    {"value", {.6, 0, 0, 0}}}}}});
        };
        if (surface_channels) {
            const auto original = scene.document();
            unsigned mesh_index = 0;
            for (const auto& row : original.at("entities")) {
                const auto id = row.at("id").get<std::string>();
                const auto& c = row.at("components");
                if (c.contains("forge.decal")) {
                    authoring_command(scene, "entity.delete", {{"entity", id}});
                    continue;
                }
                if (c.contains("forge.mesh_renderer")) {
                    auto values = parameters({.3f, .5f, .7f, 1});
                    values[1]["parameter"]["value"] = {.45, 0, 0, 0};
                    values[2]["parameter"]["value"] = {.12, 0, 0, 0};
                    set(id, "forge.mesh_renderer", {{"material_parameters", values}});
                    const char* names[]{"Floor - combined normal and roughness patch",
                                        "Left receiver - projected normal only",
                                        "Right receiver - projected roughness only",
                                        "Rear receiver - unchanged comparison"};
                    authoring_command(scene, "entity.rename",
                                      {{"entity", id}, {"name", names[mesh_index++]}});
                }
                if (c.contains("forge.camera"))
                    authoring_command(
                        scene, "entity.rename",
                        {{"entity", id}, {"name", "Camera - projected surface channels"}});
            }
            check(mesh_index == 4, "Projected channel seed lost its receivers");
            const auto normal_material = AssetId::parse("7ed3592a-fab7-41d2-a5c4-c4c7c5eda387");
            const auto add = [&](const char* name, double normal, double roughness,
                                 Double3 position, LocalRotation rotation, LocalScale scale,
                                 double width, double height, double depth, unsigned layers) {
                const auto created = authoring_command(scene, "entity.create",
                                                       {{"recipe", "render.decal"},
                                                        {"name", name},
                                                        {"component_values",
                                                         {{"material", normal_material},
                                                          {"width", width},
                                                          {"height", height},
                                                          {"depth", depth},
                                                          {"layers", layers},
                                                          {"color_influence", 0},
                                                          {"normal_influence", normal},
                                                          {"roughness_influence", roughness}}}});
                pose(created.at("selected"), position, rotation, scale);
            };
            add("Normal patch - change Normal influence, mirrored X", 1, 0, {-2, 1, -1}, {},
                {-1, 1, 1}, 1.6, 1.6, 2, 1);
            add("Roughness patch - change Roughness influence", 0, 1, {2, 1, -1}, {}, {}, 1.6, 1.6,
                2, 2);
            add("Floor detail - normal and roughness together", 1, 1, {0, .01, -.75},
                rotation_from_euler({90, 0, 0}), {}, 4, 3, 1, 3);
        } else {
            authoring_command(scene, "scene.rendering.set", {{"shadows", {{"enabled", false}}}});
            unsigned meshes = 0, lights = 0, cameras = 0;
            const auto original = scene.document();
            for (const auto& row : original.at("entities")) {
                const auto id = row.at("id").get<std::string>();
                const auto& c = row.at("components");
                std::string name = row.at("name");
                if (c.contains("forge.mesh_renderer")) {
                    const auto index = meshes++;
                    set(id, "forge.primitive", {{"kind", 0}});
                    set(id, "forge.mesh_renderer",
                        {{"mesh", engine_primitive(0).id},
                         {"layers", index == 1   ? 1
                                    : index == 2 ? 2
                                                 : 3},
                         {"cast_shadows", false},
                         {"material_parameters",
                          parameters(index == 0 ? std::array<float, 4>{.18f, .2f, .24f, 1}
                                                : std::array<float, 4>{.65f, .7f, .75f, 1})}});
                    const auto position = index == 0   ? Double3{0, -.2, 1}
                                          : index == 1 ? Double3{-2, 1, 0}
                                          : index == 2 ? Double3{2, 1, 0}
                                                       : Double3{0, .4, 2};
                    const auto scale = index == 0   ? LocalScale{12, .3f, 8}
                                       : index == 3 ? LocalScale{.8f, .8f, .8f}
                                                    : LocalScale{2, 2, 2};
                    pose(id, position, {}, scale);
                    name = index == 0   ? "Floor - overlapping projected color"
                           : index == 1 ? "Left receiver - layer 1"
                           : index == 2 ? "Right receiver - layer 2"
                                        : "Raised receiver - volume clipping";
                } else if (c.contains("forge.camera")) {
                    ++cameras;
                    pose(id, {0, 3, -8}, rotation_from_euler({15, 0, 0}));
                    name = "Gallery camera - projected color";
                } else if (c.contains("forge.light")) {
                    const auto index = lights++;
                    set(id, "forge.light",
                        {{"kind", index == 0 ? 0 : 1},
                         {"enabled", index < 2},
                         {"layers", 3},
                         {"cast_shadows", false},
                         {"intensity", index == 0 ? 2 : 8},
                         {"range", 0},
                         {"color_r", 1},
                         {"color_g", 1},
                         {"color_b", 1}});
                    pose(id, {0, 4, -3}, rotation_from_euler({35, -25, 0}));
                    name = index == 0   ? "Directional light - projected albedo"
                           : index == 1 ? "Point fill"
                                        : "Optional second fill - disabled";
                }
                authoring_command(scene, "entity.rename", {{"entity", id}, {"name", name}});
            }
            check(meshes == 4 && lights == 3 && cameras == 1, "Decal seed arrangement changed");
            std::ifstream badge_file(result.root / "Assets/Sprites/Badge.material.json");
            MaterialSource badge{Json::parse(badge_file)};
            badge.validate();
            const auto add = [&](const char* name, Json values, Double3 position,
                                 LocalRotation rotation, LocalScale scale = {}) {
                const auto created = authoring_command(
                    scene, "entity.create",
                    {{"recipe", "render.decal"}, {"name", name}, {"component_values", values}});
                pose(created.at("selected"), position, rotation, scale);
            };
            add("Badge projection - layer 1, mirrored local X",
                {{"material", badge.asset()},
                 {"width", 1.6},
                 {"height", 1.6},
                 {"depth", 1.5},
                 {"layers", 1},
                 {"priority", 0}},
                {-2, 1, -1}, {}, {-1, 1, 1});
            add("Tinted badge projection - layer 2",
                {{"material", badge.asset()},
                 {"width", 1.6},
                 {"height", 1.6},
                 {"depth", 1.5},
                 {"tint_r", .5},
                 {"tint_g", 1},
                 {"tint_b", .7},
                 {"layers", 2},
                 {"priority", 1}},
                {2, 1, -1}, {});
            add("Floor paint - opacity and volume",
                {{"material", engine_material().id},
                 {"width", 4},
                 {"height", 3},
                 {"depth", 1},
                 {"tint_r", .15},
                 {"tint_g", .55},
                 {"tint_b", .9},
                 {"opacity", .35},
                 {"layers", 3},
                 {"priority", 5}},
                {0, 0, -.75}, rotation_from_euler({90, 0, 0}));
            add("Floor badge - higher priority paints last",
                {{"material", badge.asset()},
                 {"width", 1.6},
                 {"height", 1.6},
                 {"depth", .5},
                 {"layers", 3},
                 {"priority", 6}},
                {0, .01, -.25}, rotation_from_euler({90, 0, 0}));
        }
        project.save();
    } else {
        project.open_scene(destination);
    }
    result.scene = scene.asset_id();
    const auto extracted = extract_render_scene(scene.effective_document());
    check(extracted.decals.size() == (surface_channels ? 3 : 4) && extracted.meshes.size() == 4 &&
              extracted.cameras.size() == 1 && extracted.lights.size() == 2 &&
              extracted.diagnostics.empty(),
          "Decal Gallery lost its admitted scene composition");
    const std::array roots{result.scene};
    prepare_runtime_content_catalog(*project.writer_guard(), roots, {});
    const auto after = AssetCatalog::open_project(result.root).document().at("assets");
    check(after.size() == prior.size() + std::size_t(result.created),
          "Decal Gallery changed unrelated asset counts");
    for (const auto& record : prior)
        check(std::find(after.begin(), after.end(), record) != after.end(),
              "Decal Gallery changed an existing asset record");
    check(project.settings().document() == settings, "Decal Gallery changed project settings");
    return result;
}
} // namespace forge::test_decal_scene
