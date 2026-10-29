#pragma once
#include "editor/document.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
namespace forge::test_soft_shadows {
using test_sprite_draw::check;
struct Prepared {
    std::filesystem::path root, scratch;
    AssetId scene;
    std::size_t prior_records = 0;
};
inline Prepared prepare(const std::filesystem::path& gallery,
                        const std::filesystem::path& scratch) {
    check(!std::filesystem::exists(gallery / "Scenes/111-contact-hardening-shadows.scene.json"),
          "Gallery preparation requires the input before scene111 was added");
    Prepared result;
    result.scratch = scratch / AssetId::generate().str();
    result.root = result.scratch / "Project";
    std::filesystem::create_directories(result.scratch);
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(result.root, true);
    const auto prior = AssetCatalog::open_project(result.root).document().at("assets");
    result.prior_records = prior.size();
    project.open_scene(result.root / "Scenes/02-lighting.scene.json");
    project.save_as(result.root / "Scenes/111-contact-hardening-shadows.scene.json");
    result.scene = scene.asset_id();
    authoring_command(
        scene, "scene.rendering.set",
        {{"shadows",
          {{"resolution", 1024}, {"cascades", 3}, {"max_lights", 1}, {"distance", 40}}}});
    const auto source = scene.document();
    unsigned receivers = 0, lights = 0;
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        const auto& components = row.at("components");
        if (components.contains("forge.tint")) {
            // The old blockout profile is deliberately unlit. Use the ordinary
            // Mesh Renderer/PBR material for actual lit receivers and casters.
            authoring_command(scene, "component.add",
                              {{"entity", id}, {"component", "forge.mesh_renderer"}});
            const auto& tint = components.at("forge.tint");
            const Json parameter{
                {"type", unsigned(MaterialParameterType::LinearColor4)},
                {"value", Json::array({tint.at("r"), tint.at("g"), tint.at("b"), 1.})}};
            const Json override_value{
                {"slot", "surface"}, {"key", "baseColorFactor"}, {"parameter", parameter}};
            const Json parameters = Json::array({override_value});
            authoring_command(
                scene, "property.set_many",
                {{"entity", id},
                 {"component", "forge.mesh_renderer"},
                 {"values",
                  {{"mesh",
                    engine_primitive(components.at("forge.primitive").at("kind").get<unsigned>())
                        .id},
                   {"material_parameters", parameters}}}});
        }
        if (row.at("components").contains("forge.light")) {
            const bool sun = row.at("name") == "Sun";
            authoring_command(scene, "property.set_many",
                              {{"entity", id},
                               {"component", "forge.light"},
                               {"values",
                                {{"enabled", sun},
                                 {"cast_shadows", sun},
                                 {"shadow_angular_radius", sun ? .08 : 0},
                                 {"shadow_source_radius", 0},
                                 {"shadow_bias", .001},
                                 {"shadow_normal_bias", .02}}}});
            if (sun) {
                authoring_command(
                    scene, "entity.rename",
                    {{"entity", id}, {"name", "Soft sun - change Shadow Angular Radius"}});
                ++lights;
            }
        }
        if (row.at("name").get<std::string>().ends_with("pedestal")) {
            const double height = receivers == 0 ? .5 : receivers == 1 ? 2 : 4;
            authoring_command(
                scene, "property.set",
                {{"entity", id}, {"component", "forge.local_scale"}, {"field", "y"}, {"value", 1}});
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", "forge.local_translation"},
                               {"field", "y"},
                               {"value", height}});
            authoring_command(scene, "entity.rename",
                              {{"entity", id},
                               {"name", receivers == 0   ? "Contact shadow - resting box"
                                        : receivers == 1 ? "Medium separation - raised box"
                                                         : "Wide penumbra - high box"}});
            ++receivers;
        }
    }
    check(lights == 1 && receivers == 3, "Soft shadow Gallery lost its source lighting fixture");
    project.save();
    const std::array roots{result.scene};
    prepare_runtime_content_catalog(*project.writer_guard(), roots, {});
    const auto after = AssetCatalog::open_project(result.root).document().at("assets");
    check(after.size() == prior.size() + 1,
          "Soft shadow preparation added unrelated asset records");
    for (const auto& record : prior)
        check(std::find(after.begin(), after.end(), record) != after.end(),
              "Soft shadow preparation changed an existing asset record");
    return result;
}
template <class Render>
void gallery(Render&& render, FrameRenderer& frame, FrameRenderer& ordinary,
             const RenderScene& scene, const std::filesystem::path& images) {
    auto fixed = scene;
    unsigned soft_lights = 0;
    for (auto& row : fixed.lights) {
        soft_lights += row.light.shadow_source_radius > 0 || row.light.shadow_angular_radius > 0;
        row.light.shadow_source_radius = row.light.shadow_angular_radius = 0;
    }
    check(soft_lights == 1, "Soft shadow Gallery lost its authored source size");
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto soft = render(frame, scene, width, height);
        const auto sharp = render(ordinary, fixed, width, height);
        unsigned changed = 0;
        for (std::size_t at = 0; at < soft.size(); ++at) {
            changed += soft[at] != sharp[at];
            check(soft[at][3] == sharp[at][3], "Soft shadow changed current Game alpha");
            if (sharp[at] == sharp.front())
                check(soft[at] == sharp[at], "Soft shadow changed the clear background");
        }
        check(changed > 300 && render(frame, scene, width, height) == soft,
              "Soft shadow Gallery lacks a stable rendered difference");
        for (bool enabled : {false, true}) {
            std::ofstream file(images / (std::string("soft-shadow-") +
                                         (enabled ? "enabled-" : "fixed-") + std::to_string(width) +
                                         "x" + std::to_string(height) + ".ppm"),
                               std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : enabled ? soft : sharp)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Soft shadow Gallery image write failed");
        }
        std::cout << "Soft shadow Gallery " << width << 'x' << height << ": " << changed
                  << " changed pixels; static/alpha/background PASS\n"
                  << std::flush;
    }
    for (unsigned kind : {1u, 2u}) {
        auto selected = scene;
        check(selected.lights.size() == 1, "Soft shadow fixture needs one selected light");
        auto& light = selected.lights[0].light;
        light.kind = kind;
        light.range = 20;
        light.intensity = 400;
        light.shadow_angular_radius = 0;
        light.shadow_source_radius = .5f;
        auto point = selected;
        point.lights[0].light.shadow_source_radius = 0;
        const auto soft = render(frame, selected), sharp = render(ordinary, point);
        unsigned changed = 0;
        for (std::size_t at = 0; at < soft.size(); ++at) {
            changed += soft[at] != sharp[at];
            check(soft[at][3] == sharp[at][3], "Punctual soft shadow changed alpha");
        }
        check(changed > 40 && render(frame, selected) == soft,
              "Actual punctual light has no stable contact-hardening signal");
        for (auto& mesh : selected.meshes)
            mesh.renderer.cast_shadows = false;
        point = selected;
        point.lights[0].light.shadow_source_radius = 0;
        check(render(frame, selected) == render(ordinary, point),
              "Soft shadows changed geometry without any caster");
        auto excluded = scene;
        excluded.lights[0].light = selected.lights[0].light;
        excluded.lights[0].light.layers = 0;
        point = excluded;
        point.lights[0].light.shadow_source_radius = 0;
        check(render(frame, excluded) == render(ordinary, point),
              "Excluded light layers still supplied soft shadows");
        std::cout << "Actual light kind " << kind << ": " << changed
                  << " soft-shadow pixels; static/alpha/caster/layers PASS\n"
                  << std::flush;
    }
}
} // namespace forge::test_soft_shadows
