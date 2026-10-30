#pragma once
#include "editor/document.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
namespace forge::test_area_scene {
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Prepared {
    std::filesystem::path root, scratch;
    AssetId scene;
    std::size_t prior_records = 0;
};
inline Prepared prepare(const std::filesystem::path& gallery,
                        const std::filesystem::path& scratch) {
    check(!std::filesystem::exists(gallery / "Scenes/112-area-lights.scene.json"),
          "Area preparation requires a project before Scene112 was added");
    Prepared result;
    result.scratch = scratch / AssetId::generate().str();
    result.root = result.scratch / "Project";
    std::filesystem::create_directories(result.root.parent_path());
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(result.root, true);
    const auto prior = AssetCatalog::open_project(result.root).document().at("assets");
    result.prior_records = prior.size();
    const auto settings = project.settings().document();
    project.open_scene(result.root / "Scenes/111-contact-hardening-shadows.scene.json");
    project.save_as(result.root / "Scenes/112-area-lights.scene.json");
    result.scene = scene.asset_id();
    const auto set = [&](const std::string& id, const char* component, const Json& values) {
        authoring_command(scene, "property.set_many",
                          {{"entity", id}, {"component", component}, {"values", values}});
    };
    const auto pose = [&](const std::string& id, Double3 position, LocalRotation rotation,
                          LocalScale scale) {
        authoring_command(
            scene, "transform.local",
            {{"entity", id},
             {"translation", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}},
             {"rotation",
              {{"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}}},
             {"scale", {{"x", scale.x}, {"y", scale.y}, {"z", scale.z}}}});
    };
    const auto parameters = [](std::array<float, 4> color, float metal, float roughness) {
        Json values = Json::array();
        const auto add = [&](const char* key, MaterialParameterType type, Json value) {
            values.push_back({{"slot", "surface"},
                              {"key", key},
                              {"parameter", {{"type", unsigned(type)}, {"value", value}}}});
        };
        add("baseColorFactor", MaterialParameterType::LinearColor4, color);
        add("metallicFactor", MaterialParameterType::Scalar, Json::array({metal, 0, 0, 0}));
        add("roughnessFactor", MaterialParameterType::Scalar, Json::array({roughness, 0, 0, 0}));
        return values;
    };
    authoring_command(scene, "scene.rendering.set", {{"shadows", {{"enabled", false}}}});
    unsigned meshes = 0, lights = 0, cameras = 0;
    const auto source = scene.document();
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        const auto& c = row.at("components");
        std::string name = row.at("name");
        if (c.contains("forge.mesh_renderer")) {
            const unsigned index = meshes++;
            const bool sphere = index == 1 || index == 2;
            set(id, "forge.primitive", {{"kind", sphere ? 1 : 0}});
            set(id, "forge.mesh_renderer",
                {{"mesh", engine_primitive(sphere ? 1 : 0).id},
                 {"layers", index == 1   ? 1
                            : index == 2 ? 2
                                         : 3},
                 {"cast_shadows", false},
                 {"material_parameters",
                  parameters(index == 0   ? std::array<float, 4>{.12f, .15f, .2f, 1}
                             : index == 1 ? std::array<float, 4>{.85f, .55f, .25f, 1}
                                          : std::array<float, 4>{.65f, .75f, .9f, 1},
                             sphere ? .9f : .25f, sphere ? .12f : .4f)}});
            const Double3 position = index == 0   ? Double3{0, -.15, 1}
                                     : index == 1 ? Double3{-2, 1, 0}
                                     : index == 2 ? Double3{2, 1, 0}
                                                  : Double3{0, .5, 1.5};
            const LocalScale scale = index == 0 ? LocalScale{12, .3f, 8}
                                     : sphere   ? LocalScale{2, 2, 2}
                                                : LocalScale{.8f, .8f, .8f};
            pose(id, position, {}, scale);
            name = index == 0   ? "Floor - both emitters"
                   : index == 1 ? "Warm metal - rectangle lighting"
                   : index == 2 ? "Cool metal - sphere lighting"
                                : "Rough receiver - both emitters";
        } else if (c.contains("forge.camera")) {
            ++cameras;
            pose(id, {0, 2.5, -8}, rotation_from_euler({12, 0, 0}), {});
            set(id, "forge.camera", {{"vertical_fov", std::numbers::pi / 4}, {"far_plane", 100}});
        } else if (c.contains("forge.light")) {
            const auto kind = c.at("forge.light").at("kind").get<unsigned>();
            if (kind == 0) {
                set(id, "forge.light", {{"enabled", false}, {"cast_shadows", false}});
                name = "Optional fill - disabled";
            } else {
                const bool rectangle = kind == 1;
                pose(id, rectangle ? Double3{-3, 3, -2} : Double3{3, 3, -2},
                     rectangle ? rotation_from_euler({42, 27, -20}) : LocalRotation{}, {});
                set(id, "forge.light",
                    {{"enabled", true},
                     {"kind", unsigned(rectangle ? LightKind::Rectangle : LightKind::Sphere)},
                     {"cast_shadows", false},
                     {"intensity", 50},
                     {"range", 12},
                     {"layers", rectangle ? 1 : 2},
                     {"color_r", rectangle ? 1 : .6},
                     {"color_g", rectangle ? .85 : .8},
                     {"color_b", rectangle ? .65 : 1},
                     {"emitter_width", 2.4},
                     {"emitter_height", .55},
                     {"emitter_radius", .8}});
                name = rectangle ? "Rectangle - change Emitter Width and Height"
                                 : "Sphere - change Emitter Radius";
                ++lights;
            }
        }
        authoring_command(scene, "entity.rename", {{"entity", id}, {"name", name}});
    }
    check(meshes == 4 && lights == 2 && cameras == 1,
          "Area Gallery seed lost its mesh/light/camera arrangement");
    project.save();
    const std::array roots{result.scene};
    prepare_runtime_content_catalog(*project.writer_guard(), roots, {});
    const auto after = AssetCatalog::open_project(result.root).document().at("assets");
    check(after.size() == prior.size() + 1, "Area Gallery added unrelated asset records");
    for (const auto& record : prior)
        check(std::find(after.begin(), after.end(), record) != after.end(),
              "Area Gallery changed an existing asset record");
    check(project.settings().document() == settings, "Area Gallery changed project settings");
    return result;
}
template <class Render>
void gallery(Render&& render, FrameRenderer& frame, FrameRenderer& ordinary,
             const RenderScene& scene, const std::filesystem::path& images) {
    check(scene.meshes.size() == 4 && scene.lights.size() == 2 && scene.cameras.size() == 1 &&
              scene.diagnostics.empty(),
          "Area Gallery lost its authored arrangement");
    auto small = scene;
    unsigned rectangles = 0, spheres = 0;
    for (auto& row : small.lights) {
        rectangles += row.light.kind == unsigned(LightKind::Rectangle);
        spheres += row.light.kind == unsigned(LightKind::Sphere);
        row.light.emitter_radius = row.light.emitter_width = row.light.emitter_height = .001f;
    }
    check(rectangles == 1 && spheres == 1, "Area Gallery needs both emitter shapes");
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto area = render(frame, scene, width, height);
        const auto tiny = render(ordinary, small, width, height);
        unsigned changed = 0;
        for (std::size_t at = 0; at < area.size(); ++at) {
            changed += area[at] != tiny[at];
            check(area[at][3] == tiny[at][3], "Area size changed Game alpha");
            if (tiny[at] == tiny.front())
                check(area[at] == tiny[at], "Area size changed clear background");
        }
        check(changed > 100 && render(frame, scene, width, height) == area,
              "Area Gallery lacks a stable visible emitter-size difference");
        for (bool full : {true, false}) {
            std::ofstream file(images /
                                   (std::string("area-light-") + (full ? "full-" : "tiny-") +
                                    std::to_string(width) + "x" + std::to_string(height) + ".ppm"),
                               std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : full ? area : tiny)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Area Gallery image write failed");
        }
        std::cout << "Area Gallery " << width << 'x' << height << ": " << changed
                  << " changed pixels; static/alpha/background PASS\n"
                  << std::flush;
    }
}
} // namespace forge::test_area_scene
