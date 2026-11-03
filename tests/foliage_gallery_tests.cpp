#include "editable_mesh_authoring.hpp"
#include "editable_mesh_document.hpp"
#include "mesh_scatter_authoring.hpp"
#include "runtime_package.hpp"
#include "terrain_foliage_authoring.hpp"
#include <array>
#include <cmath>
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_scene.hpp>
#include <forge/scene_identity.hpp>
#include <forge/terrain_source.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/144-foliage-placement.scene.json";
constexpr auto mesh_path = "Assets/Foliage/MeadowBlade.mesh.json";
void check(bool v, const std::string& why) {
    if (!v)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& p) {
    Json j;
    std::ifstream(p) >> j;
    return j;
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Foliage Gallery source already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto blade = [](AssetId asset) {
        auto mesh = EditableMeshSource::create_cube(asset);
        for (auto& vertex : mesh.document["vertices"]) {
            const auto v = vertex.at("position").get<std::array<double, 3>>();
            const double t = v[1] + .5;
            vertex["position"] = {v[0] * (.32 * (1 - t) + .035 * t) + .25 * t, t * 1.6, v[2] * .06};
        }
        mesh.validate();
        return mesh;
    };
    AssetId mesh_id;
    if (std::filesystem::exists(root / mesh_path)) {
        const EditableMeshSource existing{read(root / mesh_path)};
        existing.validate();
        check(existing.document == blade(existing.asset()).document,
              "Existing foliage source differs from the original blade recipe");
        mesh_id = existing.asset();
    } else {
        auto document = EditableMeshDocument::create(lease, mesh_path, blade);
        mesh_id = document->source().asset();
    }
    AssetImportService importer(lease, editable_mesh_import_registry(),
                                {"windows", "d3d12", "desktop"});
    importer.submit(
        importer.prepare(mesh_path, {}, mesh_id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_editable_mesh_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(std::chrono::seconds(10)), "Foliage Mesh cook timed out");
    const auto receipts = importer.poll();
    check(receipts.size() == 1 && receipts[0].published, "Foliage Mesh publication failed");
    EngineContext engine;
    Scene scene(engine.world());
    auto original =
        duplicate_scene_asset(read(root / "Scenes/102-temporal-antialiasing.scene.json"),
                              AssetId::generate(), scene.schema());
    std::erase_if(original["entities"].get_ref<Json::array_t&>(), [](const Json& e) {
        const auto& c = e.at("components");
        return !c.contains("forge.camera") && !c.contains("forge.light") &&
               !c.contains("forge.transform_animator");
    });
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"temporal_anti_aliasing", {{"enabled", false}}}});
    for (const auto& e : original.at("entities"))
        if (e.at("components").contains("forge.camera")) {
            authoring_command(scene, "entity.rename",
                              {{"entity", e.at("id")}, {"name", "Camera - seeded meadow"}});
            authoring_command(
                scene, "transform.position",
                {{"entity", e.at("id")}, {"value", {{"x", 0}, {"y", 7}, {"z", -15}}}});
            authoring_command(scene, "transform.rotation",
                              {{"entity", e.at("id")}, {"value", {{"x", 22}, {"y", 0}, {"z", 0}}}});
        }
    const auto floor = authoring_command(scene, "entity.create",
                                         {{"recipe", "primitive.3"},
                                          {"name", "Meadow ground"},
                                          {"position", {{"x", 0}, {"y", -.02}, {"z", 0}}}})
                           .at("selected");
    authoring_command(scene, "transform.scale",
                      {{"entity", floor}, {"value", {{"x", 15}, {"y", 1}, {"z", 10}}}});
    auto color = [](std::array<double, 4> rgba) {
        return Json::array({{{"slot", "default"},
                             {"key", "baseColorFactor"},
                             {"parameter", {{"type", 5}, {"value", rgba}}}},
                            {{"slot", "default"},
                             {"key", "roughnessFactor"},
                             {"parameter", {{"type", 0}, {"value", {.9, 0, 0, 0}}}}}});
    };
    const auto prototype =
        authoring_command(
            scene, "entity.create",
            {{"recipe", "render.mesh"},
             {"name", "Blade template - select then Tools Foliage placement"},
             {"position", {{"x", -7}, {"y", 0}, {"z", -3}}},
             {"component_values",
              {{"mesh", mesh_id}, {"material_parameters", color({.055, .55, .12, 1})}}}})
            .at("selected")
            .get<std::string>();
    detail::MeshScatterSettings s;
    s.center = {-3, 0, 0};
    s.radius = 3.8;
    s.count = 128;
    s.spacing = .42;
    s.scale_min = .65;
    s.scale_max = 1.25;
    s.seed = 73;
    authoring_command(scene, "mesh.scatter",
                      detail::mesh_scatter_arguments(prototype, s, "Green meadow"));
    authoring_command(scene, "property.set",
                      {{"entity", prototype},
                       {"component", "forge.mesh_renderer"},
                       {"field", "material_parameters"},
                       {"value", color({.65, .42, .055, 1})}});
    s.center = {4.2, 0, .4};
    s.radius = 2.1;
    s.count = 64;
    s.spacing = .32;
    s.scale_min = .45;
    s.scale_max = .8;
    s.seed = 190;
    authoring_command(scene, "mesh.scatter",
                      detail::mesh_scatter_arguments(prototype, s, "Amber meadow"));
    // Template edits remain independent from already placed groups.
    authoring_command(scene, "property.set",
                      {{"entity", prototype},
                       {"component", "forge.mesh_renderer"},
                       {"field", "material_parameters"},
                       {"value", color({.055, .55, .12, 1})}});
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
#include "terrain_foliage_gallery.hpp"
void exercise(const std::filesystem::path& root, AssetId id,
              const Json& expected = Json::object()) {
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& w) override {
            return {w.prepare_scene_resources(), "foliage resources"};
        }
        void activate() noexcept override {}
    };
    GameSessionConfig config;
    config.content_root = root;
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession game(config);
    const auto candidate = game.prepare(load_game_scene(root, {id}));
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!game.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < end, "Foliage resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(candidate, {}, false);
    auto& world = game.active();
    const auto authored = world.scene.effective_document();
    if (!expected.empty()) {
        unsigned count = 0;
        for (const auto& e : authored.at("entities"))
            if (e.at("name").get<std::string>().starts_with("Terrain meadow ")) {
                const auto& point = expected.at(e.at("id").get<std::string>());
                const auto& m = e.at("world_affine");
                for (unsigned axis = 0; axis < 3; ++axis)
                    check(std::abs(m[axis * 4 + 3].get<double>() - point[axis].get<double>()) <
                              1e-8,
                          "Runtime foliage no longer rests on its authored terrain surface");
                ++count;
            }
        world.simulation.tick(1.f / 60);
        const auto render = extract_render_scene(world.simulation.presentation(1));
        check(count == 128 && render.meshes.size() == 130 && render.cameras.size() == 1 &&
                  !render.lights.empty(),
              "Terrain foliage lost its ordinary runtime renderers/camera/light");
        std::cout << "Terrain foliage:128 projected origins and actual runtime resources ready\n";
        return;
    }
    unsigned green = 0, amber = 0;
    flecs::entity moving;
    for (const auto& e : authored.at("entities")) {
        const auto name = e.at("name").get<std::string>();
        if (name.starts_with("Green meadow "))
            ++green;
        if (name.starts_with("Amber meadow "))
            ++amber;
        const auto entity = world.scene.entity(e.at("id"));
        if (entity.has<TransformAnimator>())
            moving = entity;
    }
    check(green == 128 && amber == 64 && bool(moving), "Foliage layout or animation missing");
    const auto before = moving.get<LocalTranslation>();
    for (unsigned tick = 0; tick < 30; ++tick)
        world.simulation.tick(1.f / 60);
    const auto after = moving.get<LocalTranslation>();
    check(std::abs(after.x - before.x) + std::abs(after.y - before.y) +
                  std::abs(after.z - before.z) >
              .01,
          "Gallery marker did not move");
    const auto render = extract_render_scene(world.simulation.presentation(1));
    check(render.meshes.size() == 195 && render.cameras.size() == 1 && !render.lights.empty(),
          "Foliage ordinary runtime renderers disappeared");
    std::cout << "Foliage:128 green,64 amber, independent template and ground, actual runtime "
                 "resources ready\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            author(std::filesystem::absolute(argv[2]));
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--author-terrain") {
            author_terrain_meadow(std::filesystem::absolute(argv[2]));
            return 0;
        }
        check(argc == 3, "Need Gallery and scratch paths");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto id = read(source / scene_path).at("asset_id").get<AssetId>();
        const auto terrain = read(source / terrain_scene_path).at("asset_id").get<AssetId>();
        const auto expected = terrain_meadow_oracle(source);
        exercise(source, id);
        exercise(source, terrain, expected);
        const std::array roots{id, terrain};
        package_runtime_content(source, scratch / "export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "export", scratch / "relocated");
        std::filesystem::rename(source, scratch / "source-offline");
        exercise(scratch / "relocated", id);
        exercise(scratch / "relocated", terrain, expected);
        std::cout << "Foliage source-hidden relocated Windows-target content passed on Linux\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
