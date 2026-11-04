#include "foliage_collision.hpp"
#include "mesh_scatter_authoring.hpp"
#include "runtime_package.hpp"
#include "terrain_foliage_authoring.hpp"
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr auto path = "Scenes/156-foliage-clearance.scene.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& file) {
    Json result;
    std::ifstream(file) >> result;
    return result;
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / path), "Clearance Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
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
    }
    check(!prototype.empty() && !surface.empty(), "Clearance Gallery actors missing");
    const auto obstacle = authoring_command(scene, "entity.create",
                                            {{"recipe", "primitive.0"},
                                             {"name", "Clearance obstacle - ignore to compare"}})
                              .at("selected");
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", 4}, {"z", 0}}}});
    authoring_command(scene, "transform.scale",
                      {{"entity", obstacle}, {"value", {{"x", 2}, {"y", 8}, {"z", 12}}}});
    for (auto component : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", component}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    detail::MeshScatterSettings settings;
    settings.center = {0, 0, 0};
    settings.radius = 5;
    settings.spacing = .5;
    settings.count = 96;
    settings.seed = 91;
    settings.scale_min = .65;
    settings.scale_max = 1.15;
    const auto prepared = prepare_terrain_foliage(
        *lease, scene, detail::mesh_scatter_arguments(prototype, settings, "Cleared meadow"),
        surface, 0, {}, true);
    FoliageCollisionPreparation task;
    FoliageCollisionSettings config;
    config.surface = EntityId::parse(surface);
    task.start(lease, scene, prepared.at("arguments"), config);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (task.busy()) {
        task.poll(scene, root);
        check(std::chrono::steady_clock::now() < deadline, "Gallery clearance timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto ready = task.read(scene, root);
    check(ready.at("state") == "ready", ready.dump().c_str());
    const auto count = ready.at("result").at("retained_count").get<unsigned>();
    check(count > 0 && count < settings.count, "Gallery clearance did not reject a proper subset");
    apply_authoring(scene,
                    Json::array({{{"operation", "mesh.scatter"},
                                  {"arguments", ready.at("result").at("arguments")}}}),
                    ready.at("result").at("scene_revision"));
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Authored clearance Gallery: " << count << "/96 retained\n";
}
void exercise(const std::filesystem::path& root, AssetId id) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.prepare_physics_assets()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery physics resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.physics()->synchronize(0);
    const auto source = world.scene.effective_document();
    unsigned plants = 0, obstacles = 0;
    for (const auto& row : source.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Clearance obstacle"))
            ++obstacles;
        if (!name.starts_with("Cleared meadow "))
            continue;
        const auto& m = row.at("world_affine");
        PhysicsOverlap query;
        query.origin = {m[3], m[7].get<double>() + .5, m[11]};
        query.dimensions = {.25, 0, 0};
        check(!world.physics()->overlap_any(query, {UINT32_MAX, false}),
              "Saved Gallery plant overlaps the actual native collider");
        ++plants;
    }
    check(plants > 0 && plants < 96 && obstacles == 1 && world.physics()->status().at("tick") == 0,
          "Clearance Gallery lacks ordinary saved plants/collider or progressed physics");
    const auto rendered = extract_render_scene(world.simulation.presentation(1));
    check(rendered.meshes.size() == plants + 3 && rendered.cameras.size() == 1 &&
              !rendered.lights.empty(),
          "Clearance Gallery ordinary rendering intent missing");
    std::cout << "Clearance Gallery actual native collision: " << plants << " saved plants clear\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            author(argv[2]);
            return 0;
        }
        check(argc == 3, "Need Gallery and scratch paths");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto id = read(root / path).at("asset_id").get<AssetId>();
        exercise(root, id);
        const std::array roots{id};
        package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Content", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        exercise(scratch / "Relocated", id);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
