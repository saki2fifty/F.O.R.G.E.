#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr auto path = "Scenes/169-occlusion-culling.scene.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / path), "Occlusion Gallery already exists");
    const auto before = AssetCatalog::open_project(root).document().at("assets");
    EngineContext engine;
    Scene scene(engine.world());
    authoring_command(scene, "scene.rendering.set",
                      {{"occlusion_culling", {{"enabled", true}, {"depth_bias", .0001}}},
                       {"shadows", {{"enabled", false}}}});
    const auto create = [&](const std::string& name, unsigned kind, LocalTranslation position,
                            LocalScale scale, Tint tint) {
        const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                            .at("selected")
                            .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>(position).set<LocalScale>(scale).set<Tint>(tint);
        return id;
    };
    create("Floor", 0, {0, -.45, 3}, {18, .3, 16}, {.16f, .20f, .25f});
    const auto moving = create("Animated occluder pivot - shared transform clip", no_primitive, {},
                               {1, 1, 1}, {1, 1, 1});
    TransformAnimator animation;
    animation.clip = {AssetId::parse("399c3c5e-f4e9-4ccb-94b3-bc0e518c1ece")};
    animation.playback_speed = .35f;
    scene.entity(moving).set<TransformAnimator>(animation);
    const auto wall = create("Moving opaque wall - reveals hidden spheres", 0, {0, 0, 0},
                             {3.2, 9, .35}, {.15f, .45f, .75f});
    authoring_command(scene, "entity.reparent",
                      {{"entity", wall}, {"parent", moving}, {"mode", "keep_local"}});
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 8; ++column)
            create("Rigid sphere " + std::to_string(row * 8 + column + 1), 1,
                   {double(column) * 1.4 - 4.9, .7, double(row) * 1.4 + 3}, {.7, .7, .7},
                   {column % 2 ? .95f : .25f, .65f, column % 2 ? .2f : .9f});
    create("Visible left landmark", 11, {-6.5, 1.1, -1}, {1.2, 1.2, 1.2}, {.9f, .3f, .4f});
    create("Visible right landmark", 0, {6.5, 1.1, -1}, {1.2, 1.2, 1.2}, {.3f, .85f, .45f});
    const auto camera_id = create("Game camera - moving occluder laboratory", no_primitive,
                                  {0, 3, -14}, {1, 1, 1}, {1, 1, 1});
    Camera camera;
    camera.far_plane = 100;
    camera.aspect = 16. / 9;
    scene.entity(camera_id).set<Camera>(camera).set<LocalRotation>(rotation_from_euler({6, 0, 0}));
    const auto sun = create("Directional light", no_primitive, {0, 6, -4}, {1, 1, 1}, {1, 1, 1});
    Light light;
    light.intensity = 3;
    light.cast_shadows = false;
    scene.entity(sun).set<Light>(light).set<LocalRotation>(rotation_from_euler({35, -25, 0}));
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    const auto selected = catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
    const ProjectLease lease(root);
    const std::array roots{selected.id};
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    check(after.size() == before.size() + 1, "Occlusion Gallery changed unrelated asset count");
    for (const auto& record : before)
        check(std::find(after.begin(), after.end(), record) != after.end(),
              "Occlusion Gallery changed an existing asset record");
}
void exercise(const std::filesystem::path& root, AssetId id) {
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    GameSessionConfig config;
    config.content_root = root;
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession game(config);
    const auto candidate = game.prepare(load_game_scene(root, {id}));
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!game.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < end, "Occlusion Gallery resource stall");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(candidate, {}, false);
    auto& world = game.active();
    const auto before = extract_render_scene(world.simulation.presentation(1));
    check(before.settings.occlusion_culling == SceneOcclusionCulling{true, .0001f} &&
              before.meshes.size() == 36 && before.cameras.size() == 1 &&
              before.lights.size() == 1 && before.diagnostics.empty(),
          "Occlusion Gallery lost controls or presentation geometry");
    for (unsigned tick = 0; tick < 60; ++tick)
        world.simulation.tick(1.f / 60);
    const auto after = extract_render_scene(world.simulation.presentation(1));
    check(after.meshes.size() == before.meshes.size(), "Occlusion animation lost Meshes");
    bool moved = false;
    for (unsigned i = 0; i < before.meshes.size(); ++i)
        moved = moved || before.meshes[i].world != after.meshes[i].world;
    check(moved && scene_render_settings(world.scene.snapshot()) == after.settings,
          "Occluder did not animate or runtime snapshot lost culling settings");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected Gallery and isolated scratch directory, or --author Gallery");
        if (std::string_view(argv[1]) == "--author") {
            author(std::filesystem::absolute(argv[2]));
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto id = read(source / path).at("asset_id").get<AssetId>();
        exercise(source, id);
        const std::array roots{id};
        package_runtime_content(source, scratch / "export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "export", scratch / "relocated");
        std::filesystem::rename(source, scratch / "source-offline");
        exercise(scratch / "relocated", id);
        std::cout << "Occlusion Gallery settings and moving rigid geometry survive source-hidden "
                     "relocated Windows-target content on Linux\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
