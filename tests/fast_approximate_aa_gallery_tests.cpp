#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_scene.hpp>
#include <forge/scene_identity.hpp>
#include <forge/transform_components.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
bool morphological = false;
const char* scene_path = "Scenes/143-fast-approximate-antialiasing.scene.json";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Gallery source already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    // The ordinary Save As identity owner remaps every entity/reference. Reuse
    // existing original edge geometry and its shared animation clip as content.
    scene.reset(duplicate_scene_asset(read(root / "Scenes/102-temporal-antialiasing.scene.json"),
                                      AssetId::generate(), scene.schema()));
    Json controls = {
        {"temporal_anti_aliasing", {{"enabled", false}}},
        {"fast_approximate_anti_aliasing",
         {{"enabled", !morphological}, {"subpixel_strength", .5}, {"search_steps", 12}}}};
    if (morphological)
        controls["morphological_anti_aliasing"] = {{"enabled", true},
                                                   {"edge_threshold", .1},
                                                   {"search_steps", 16},
                                                   {"corner_rounding", 0}};
    authoring_command(scene, "scene.rendering.set", controls);
    const auto authored = scene.document();
    for (const auto& row : authored.at("entities"))
        if (row.at("components").contains("forge.camera"))
            authoring_command(scene, "entity.rename",
                              {{"entity", row.at("id")},
                               {"name", morphological ? "Camera - morphological edge laboratory"
                                                      : "Camera - spatial edge laboratory"}});
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto selected = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{selected.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise(const std::filesystem::path& root, AssetId scene_id) {
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
    const auto candidate = game.prepare(load_game_scene(root, {scene_id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!game.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline,
              "Spatial AA Gallery resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(candidate, {}, false);
    auto& world = game.active();
    flecs::entity moving;
    const auto authored = world.scene.effective_document();
    for (const auto& row : authored.at("entities")) {
        const auto entity = world.scene.entity(row.at("id"));
        if (entity.has<TransformAnimator>())
            moving = entity;
    }
    check(bool(moving), "Spatial AA Gallery has no animated foreground");
    const auto before = moving.get<LocalTranslation>();
    for (unsigned tick = 0; tick < 30; ++tick)
        world.simulation.tick(1.f / 60);
    const auto after = moving.get<LocalTranslation>();
    check(std::abs(after.x - before.x) + std::abs(after.y - before.y) +
                  std::abs(after.z - before.z) >
              .01,
          "Spatial AA Gallery foreground did not move");
    const auto render = extract_render_scene(world.simulation.presentation(1));
    const bool selected = morphological ? render.settings.morphological_anti_aliasing ==
                                              SceneMorphologicalAntiAliasing{true, .1f, 16, 0}
                                        : render.settings.fast_approximate_anti_aliasing ==
                                              SceneFastApproximateAntiAliasing{true, .5f, 12};
    check(selected && !render.settings.temporal_anti_aliasing.enabled &&
              render.meshes.size() >= 20 && render.cameras.size() == 1 && !render.lights.empty(),
          "Spatial AA Gallery lost authored display settings or actual presentation");
    check(scene_render_settings(world.scene.snapshot()) == render.settings,
          "Runtime snapshot lost spatial AA settings");
}
} // namespace
int main(int argc, char** argv) {
    try {
        morphological = argc == 4 && std::string_view(argv[3]) == "--morphological";
        if (morphological) {
            --argc;
            scene_path = "Scenes/168-morphological-antialiasing.scene.json";
        }
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            author(std::filesystem::absolute(argv[2]));
            return 0;
        }
        check(argc == 3, "Provide Gallery and private scratch paths");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto scene_id = read(source / scene_path).at("asset_id").get<AssetId>();
        exercise(source, scene_id);
        const std::array roots{scene_id};
        package_runtime_content(source, scratch / "export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "export", scratch / "relocated");
        std::filesystem::rename(source, scratch / "source-offline");
        exercise(scratch / "relocated", scene_id);
        std::cout << "Spatial AA Gallery: current display settings and animation survive "
                     "source-hidden relocated Windows-target content on Linux\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
