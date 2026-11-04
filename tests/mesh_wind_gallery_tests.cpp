#include "mesh_scatter_authoring.hpp"
#include "render_values.hpp"
#include "runtime_package.hpp"
#include "terrain_foliage_authoring.hpp"
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
constexpr auto path = "Scenes/157-foliage-wind.scene.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& file) {
    Json value;
    std::ifstream(file) >> value;
    return value;
}
void presentation(Scene& scene) {
    const auto source = scene.document();
    Json blade;
    for (const auto& row : source.at("entities"))
        if (row.at("name").get<std::string>().starts_with("Blade template"))
            blade = row.at("components").at("forge.mesh_renderer").at("mesh");
    check(!blade.is_null(), "Wind Gallery blade missing");
    for (const auto& row : source.at("entities")) {
        const auto& components = row.at("components");
        const auto set = [&](const char* component, const char* field, Json value) {
            authoring_command(scene, "property.set",
                              {{"entity", row.at("id")},
                               {"component", component},
                               {"field", field},
                               {"value", std::move(value)}});
        };
        if (components.contains("forge.camera"))
            authoring_command(
                scene, "transform.position",
                {{"entity", row.at("id")}, {"value", {{"x", 0}, {"y", 6.5}, {"z", -8}}}});
        if (components.contains("forge.light"))
            set("forge.light", "intensity", 3);
        if (components.contains("forge.mesh_renderer") &&
            components.at("forge.mesh_renderer").at("mesh") == blade) {
            auto parameters = components.at("forge.mesh_renderer").at("material_parameters");
            std::erase_if(parameters.get_ref<Json::array_t&>(), [](const Json& parameter) {
                return parameter.at("slot") == "default" && parameter.at("key") == "metallicFactor";
            });
            parameters.push_back({{"slot", "default"},
                                  {"key", "metallicFactor"},
                                  {"parameter", {{"type", 0}, {"value", {0, 0, 0, 0}}}}});
            set("forge.mesh_renderer", "material_parameters", std::move(parameters));
        }
    }
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / path), "Wind Gallery already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read(root / "Scenes/147-terrain-foliage.scene.json"),
                                        AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto name = row.at("name").get<std::string>();
        return !row.at("components").contains("forge.camera") &&
               !row.at("components").contains("forge.light") &&
               !name.starts_with("Blade template") && !name.starts_with("Layered terrain");
    });
    scene.reset(source);
    std::string prototype, surface;
    for (const auto& row : source.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Blade template"))
            prototype = row.at("id");
        if (name.starts_with("Layered terrain"))
            surface = row.at("id");
        if (row.at("components").contains("forge.light"))
            authoring_command(scene, "property.set",
                              {{"entity", row.at("id")},
                               {"component", "forge.light"},
                               {"field", "cast_shadows"},
                               {"value", true}});
    }
    check(!prototype.empty() && !surface.empty(), "Wind Gallery source actors missing");
    authoring_command(
        scene, "scene.rendering.set",
        {{"shadows", {{"enabled", true}, {"resolution", 256}, {"cascades", 2}, {"max_lights", 1}}},
         {"temporal_anti_aliasing", {{"enabled", false}}}});
    MeshWind wind;
    wind.enabled = true;
    wind.amplitude = .6;
    wind.direction_z = .35;
    wind.frequency = .6;
    wind.wavelength = 7;
    wind.phase = 15;
    const auto tune = [&](const MeshWind& value) {
        authoring_command(scene, "property.set",
                          {{"entity", prototype},
                           {"component", "forge.mesh_renderer"},
                           {"field", "wind"},
                           {"value", detail::render_value(value)}});
    };
    const auto place = [&](const char* name, double x, unsigned count, double radius,
                           unsigned seed) {
        detail::MeshScatterSettings settings;
        settings.center = {x, 0, 0};
        settings.count = count;
        settings.radius = radius;
        settings.spacing = .35;
        settings.scale_min = .7;
        settings.scale_max = 1.35;
        settings.seed = seed;
        const auto prepared = prepare_terrain_foliage(
            *lease, scene, detail::mesh_scatter_arguments(prototype, settings, name), surface, 0,
            {}, true);
        (void)apply_authoring(
            scene,
            Json::array({{{"operation", "mesh.scatter"}, {"arguments", prepared.at("arguments")}}}),
            prepared.at("scene_revision"));
    };
    tune(wind);
    place("Wind meadow", -3, 64, 2.7, 512);
    wind.enabled = false;
    tune(wind);
    place("Still comparison", 3.5, 24, 2, 913);
    wind.enabled = true;
    tune(wind);
    authoring_command(
        scene, "entity.rename",
        {{"entity", prototype}, {"name", "Blade template - Wind controls in Inspector"}});
    presentation(scene);
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise(const std::filesystem::path& root, AssetId id) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {id}));
    const auto source = world.scene.document();
    const auto initial = world.simulation.presentation(1, 0),
               later = world.simulation.presentation(1, .75);
    check(initial.at("simulation_seconds") == 0 && later.at("simulation_seconds") == .75 &&
              initial.at("entities") == later.at("entities") && world.scene.document() == source,
          "Wind geometry sampling changed source/native transforms or failed borrowed time");
    const auto render = extract_render_scene(later);
    unsigned moving = 0, still = 0;
    for (const auto& mesh : render.meshes) {
        if (mesh.renderer.wind.enabled)
            ++moving;
        else
            ++still;
    }
    check(moving == 65 && still == 25 && render.cameras.size() == 1 && render.lights.size() == 1 &&
              render.lights.front().light.cast_shadows && render.settings.shadows.enabled,
          "Wind Gallery lacks its ordinary moving/still meshes, camera or shadows");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            author(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--presentation") {
            EngineContext engine;
            Scene scene(engine.world());
            scene.reset(read(std::filesystem::path(argv[2]) / path));
            presentation(scene);
            scene.save(std::filesystem::path(argv[2]) / path);
            return 0;
        }
        check(argc == 3, "Need Gallery and scratch roots");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str(),
                   root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto id = read(root / path).at("asset_id").get<AssetId>();
        exercise(root, id);
        const std::array roots{id};
        package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(root, scratch / "SourcesOffline");
        std::filesystem::rename(scratch / "Content", scratch / "Relocated");
        const auto catalog = open_runtime_content(scratch / "Relocated", {"windows", "d3d12"});
        check(catalog.records()
                      .at(id)
                      .metadata.at("forge.runtime_document")
                      .at("runtime_features")
                      .at("mesh_wind") == mesh_wind_profile_version,
              "Wind Gallery package omitted its runtime geometry profile");
        exercise(scratch / "Relocated", id);
        std::cout << "Wind Gallery moving/still intent, immutable source and relocated profile "
                     "admission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
