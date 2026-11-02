#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/142-prefab-variants.scene.json";
constexpr auto base_path = "Assets/Variants/SlidingBeacon.prefab.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
flecs::entity create(Scene& scene, const char* name, unsigned kind = 4) {
    return scene.entity(authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                            .at("selected")
                            .get<std::string>());
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path) && !std::filesystem::exists(root / base_path),
          "Gallery authoring requires new source paths");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto catalog = AssetCatalog::open_project(root);
    AssetId clip;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/Sequences/SlidingDoor.animation.json")
            clip = id;
    check(bool(clip), "Gallery sliding clip is missing");
    EngineContext engine;
    Scene scene(engine.world());
    const auto controller = create(scene, "Base beacon - sequence 1x");
    AnimationSequence sequence;
    sequence.duration = 2;
    sequence.loop = true;
    controller.set<AnimationSequence>(sequence);
    const auto shape = create(scene, "Sliding beacon body", 0);
    shape.set<LocalScale>({.8f, .8f, .8f}).set<Tint>({.08f, .8f, 1.f});
    TransformAnimator track;
    track.clip = {clip};
    track.sequence = scene.reference(controller.get<PersistentEntityId>().value.str());
    shape.set<TransformAnimator>(track);
    scene.reparent_entity(shape.get<PersistentEntityId>().value.str(),
                          controller.get<PersistentEntityId>().value.str(),
                          ReparentMode::KeepLocal);
    PrefabLibrary library(root);
    library.refresh(scene);
    const auto captured =
        create_prefab_source(scene, controller.get<PersistentEntityId>().value.str());
    const auto base = library.create(scene, captured, base_path);
    PrefabMemberId body;
    for (const auto& member : captured.source.at("members"))
        if (member.at("name") == "Sliding beacon body")
            body = member.at("id").get<PrefabMemberId>();
    check(bool(body), "Source capture lost the beacon member");
    auto variant = [&](AssetId parent, const char* path, const char* name, float speed, Tint tint) {
        const auto asset = library.create_variant(scene, parent, path);
        const auto expected = library.source(asset);
        auto next = expected;
        next["overrides"] = Json::array(
            {{{"id", captured.root()},
              {"name", name},
              {"components", Json::object()},
              {"property_overrides", {{"forge.animation_sequence", {{"playback_speed", speed}}}}}},
             {{"id", body},
              {"components", Json::object()},
              {"property_overrides",
               {{"forge.tint", {{"r", tint.r}, {"g", tint.g}, {"b", tint.b}}}}}}});
        library.publish(scene, expected, next, library.base_sources(scene, expected));
        return asset;
    };
    const auto amber = variant(base, "Assets/Variants/AmberBeacon.prefab.json",
                               "Amber variant - sequence 1.5x", 1.5f, {1.f, .5f, .08f});
    const auto blue = variant(base, "Assets/Variants/BlueBeacon.prefab.json",
                              "Blue variant - sequence 0.5x", .5f, {.15f, .35f, 1.f});
    const auto pink = variant(amber, "Assets/Variants/PinkBeacon.prefab.json",
                              "Pink chained variant - sequence 2x", 2, {1.f, .2f, .65f});
    authoring_command(scene, "entity.delete",
                      {{"entity", controller.get<PersistentEntityId>().value.str()}});
    const std::array assets{base, amber, blue, pink, blue};
    for (unsigned i = 0; i < assets.size(); ++i) {
        const auto id = instantiate_prefab(scene, assets[i]);
        authoring_command(scene, "transform.position",
                          {{"entity", id}, {"value", {{"x", -10. + 5 * i}, {"y", 0}, {"z", 0}}}});
    }
    create(scene, "Stage", 0)
        .set<LocalTranslation>({0, -.2, 0})
        .set<LocalScale>({28, .2f, 6})
        .set<Tint>({.045f, .08f, .11f});
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 18;
    camera.aspect = 16. / 9;
    create(scene, "Overview Camera")
        .set<LocalTranslation>({0, 10, -24})
        .set<LocalRotation>(rotation_from_euler({20, 0, 0}))
        .set<Camera>(camera);
    Light sun;
    sun.cast_shadows = true;
    create(scene, "Sunlight").set<Light>(sun).set<LocalRotation>(rotation_from_euler({55, -25, 0}));
    scene.save(root / scene_path);
    for (const auto& [id, record] : library.records())
        if (!catalog.records().contains(id))
            catalog.add(record);
    const auto selected = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{selected.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void activate(GameSession& game, const Json& snapshot) {
    const auto candidate = game.prepare(snapshot);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!game.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery variant resources timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    game.activate(candidate, {}, false);
}
void exercise(const std::filesystem::path& root, AssetId scene_id) {
    const auto snapshot = load_game_scene(root, {scene_id});
    check(snapshot.at("_prefab_sources").size() == 4, "Gallery variant base closure is incomplete");
    GameSessionConfig config;
    config.content_root = root;
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession game(config);
    activate(game, snapshot);
    auto& world = game.active();
    std::vector<flecs::entity> bodies;
    std::vector<flecs::entity> sequences;
    std::vector<double> before;
    const auto effective = world.scene.effective_document();
    for (const auto& row : effective.at("entities")) {
        const auto entity = world.scene.entity(row.at("id"));
        if (entity.has<TransformAnimator>()) {
            const auto& track = entity.get<TransformAnimator>();
            check(track.sequence == world.scene.reference(row.at("parent")),
                  "Variant animation borrowed a different instance's controller");
            bodies.push_back(entity);
            before.push_back(entity.get<LocalTranslation>().x);
        }
        if (entity.has<AnimationSequence>())
            sequences.push_back(entity);
    }
    check(bodies.size() == 5 && sequences.size() == 5,
          "Gallery lost independent variant instances");
    for (unsigned i = 0; i < 30; ++i)
        world.simulation.tick(1.f / 60);
    unsigned moved = 0;
    for (unsigned i = 0; i < bodies.size(); ++i)
        moved += std::abs(bodies[i].get<LocalTranslation>().x - before[i]) > .1;
    check(moved == 5, "Every base/variant instance must animate");
    const auto render = extract_render_scene(world.simulation.presentation(1));
    check(render.meshes.size() == 6 && render.cameras.size() == 1 && render.lights.size() == 1,
          "Variant Gallery has incomplete presentation");
    const auto animation =
        std::static_pointer_cast<AnimationRuntime>(world.engine.services().animation());
    const auto checkpoint = animation->checkpoint();
    GameSession recovered(config);
    activate(recovered, world.scene.snapshot());
    auto& restored = recovered.active();
    std::static_pointer_cast<AnimationRuntime>(restored.engine.services().animation())
        ->restore(checkpoint);
    for (unsigned tick = 0; tick < 15; ++tick) {
        world.simulation.tick(1.f / 60);
        restored.simulation.tick(1.f / 60);
        for (const auto& body : bodies) {
            const auto id = body.get<PersistentEntityId>().value.str();
            const auto other = restored.scene.entity(id);
            check(other &&
                      other.get<TransformAnimator>().sequence ==
                          body.get<TransformAnimator>().sequence &&
                      std::abs(other.get<LocalTranslation>().x - body.get<LocalTranslation>().x) <
                          1e-5,
                  "Variant animation recovery lost controller identity or playback phase");
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
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
        const auto output = scratch / "export";
        package_runtime_content(source, output, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(source, scratch / "source-offline");
        exercise(relocated, scene_id);
        std::cout << "Gallery142 variants animate with local member links, recovery and relocated "
                     "source-offline Windows-target content on Linux\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
