#include "editor/document.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/237-runtime-navigation-rebuild.scene.json";
constexpr auto clip_path = "Assets/Navigation/MoveFloorOnce.animation.json";
constexpr auto floor_name = "Floor - moves once, then rebuild navigation";
constexpr auto actor_name = "Cyan Agent - resumes after runtime rebuild";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
flecs::entity create(Scene& scene, const char* name, Double3 position = {}, unsigned kind = 4) {
    const auto result = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}});
    const auto entity = scene.entity(result.at("selected").get<std::string>());
    entity.set<LocalTranslation>({position[0], position[1], position[2]});
    return entity;
}
void author(const std::filesystem::path& root, const std::filesystem::path& worker) {
    check(!std::filesystem::exists(root / scene_path), "Runtime rebuild Gallery already exists");
    const auto previous = AssetCatalog::open_project(root).document().at("assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    document.new_scene();
    auto animation = TransformAnimationDocument::create(document.writer_guard(), clip_path);
    animation->edit_source(
        animation->revision(), "One-shot floor translation", [](TransformAnimationSource& source) {
            source.retime_duration(animation_timebase);
            source.document["channels"] = Json::array({"forge.local_translation"});
            source.set_key(0, {});
            source.set_key(animation_timebase, {{2, 0, 0}, {}, {}});
        });
    animation->save();
    const auto clip = animation->source().asset();
    AssetImportService importer(document.writer_guard(), transform_animation_import_registry(),
                                {"windows", "d3d12", "desktop"});
    importer.submit(
        importer.prepare(clip_path, {}, clip),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(std::chrono::seconds(20)), "Gallery floor clip cook timed out");
    const auto outcomes = importer.poll();
    check(outcomes.size() == 1 && outcomes.front().published, "Gallery floor clip did not publish");
    TransformAnimator animator{{clip}};
    animator.loop = false;
    create(scene, floor_name, {}, 3)
        .set<LocalScale>({18, 1, 10})
        .set<Tint>({.08f, .2f, .28f})
        .set<NavigationSurface>({true})
        .set(animator);
    const auto actor = create(scene, actor_name, {-5, .1, 0});
    const auto body = create(scene, "Cyan Agent body", {0, .3, 0}, 0);
    body.set<LocalScale>({.6f, .6f, .6f}).set<Tint>({.05f, .8f, 1.f});
    scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                          actor.get<PersistentEntityId>().value.str(), ReparentMode::KeepLocal);
    create(scene, "Green destination", {5, .04, 0}, 0)
        .set<LocalScale>({.8f, .08f, .8f})
        .set<Tint>({.2f, .9f, .35f});
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 16;
    camera.aspect = 16. / 9;
    create(scene, "Overview Camera", {13, 13, -16})
        .set(camera)
        .set<LocalRotation>(rotation_from_euler({32.3, -39.1, 0}));
    Light light;
    light.cast_shadows = true;
    create(scene, "Sunlight").set(light).set<LocalRotation>(rotation_from_euler({55, -25, 0}));
    document.save_as(scene_path); // Preserve the final Scene identity before baking.
    const auto effective = scene.effective_document();
    const auto navigation = prepare_navigation(root, effective, {}, worker).publish(effective);
    actor.set<NavigationAgent>({{navigation.id}, true, true, 2, .08f, 5, .1, 0});
    document.save();
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*document.writer_guard(), roots, {}, scene.schema());
    const auto current = AssetCatalog::open_project(root).document().at("assets");
    for (const auto& row : previous)
        check(std::find(current.begin(), current.end(), row) != current.end(),
              "New Gallery content changed an existing record");
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path,
              const std::filesystem::path& worker) {
    const auto authored = read(root / path);
    GameSessionConfig config;
    config.content_root = root;
    config.navigation_worker = worker;
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession session(config);
    const auto candidate = session.prepare(authored);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery resource admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    flecs::entity actor, floor;
    for (const auto& row : authored.at("entities")) {
        if (row.at("name") == actor_name)
            actor = world.scene.entity(row.at("id"));
        if (row.at("name") == floor_name)
            floor = world.scene.entity(row.at("id"));
    }
    check(actor && floor, "Gallery lost its editable floor or Agent");
    const auto navigation = world.engine.services().navigation();
    const auto asset = actor.get<NavigationAgent>().navmesh;
    for (unsigned tick = 0; tick < 90; ++tick)
        session.step();
    const auto stopped = actor.get<LocalTranslation>();
    check(std::abs(floor.get<LocalTranslation>().x - 2) < 1e-6 &&
              navigation->find_path(asset, {-5, .1, 0}, {5, .1, 0}).status == NavStatus::Stale,
          "One-shot floor did not finish or invalidate the original basis");
    for (unsigned tick = 0; tick < 10; ++tick)
        session.step();
    check(actor.get<LocalTranslation>() == stopped, "Agent moved on stale floor geometry");
    const auto clock = session.status().at("clock");
    const auto token = navigation->request_rebuild(asset);
    const auto finish = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (navigation->rebuild_state(token).phase <= NavigationRebuildPhase::Building) {
        session.control_frame();
        check(std::chrono::steady_clock::now() < finish, "Gallery runtime rebake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto result = navigation->rebuild_state(token);
    check(result.phase == NavigationRebuildPhase::Applied,
          "Gallery runtime rebuild rejected: " + result.diagnostic);
    check(session.status().at("clock") == clock && actor.get<LocalTranslation>() == stopped,
          "Paused Gallery rebake advanced gameplay time or pose");
    for (unsigned tick = 0; tick < 400; ++tick)
        session.step();
    const auto observed =
        navigation->agent_state(world.scene.reference(actor.get<PersistentEntityId>().value.str()));
    check(observed && observed->phase == NavigationAgentPhase::Arrived &&
              std::abs(actor.get<LocalTranslation>().x - 5) <= .1,
          "Agent did not resume and arrive after private query adoption");
    check(read(root / path) == authored, "Runtime Gallery flow saved transient source values");
    session.unload({});
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            author(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 4, "Need Gallery, scratch and matching worker");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto worker = std::filesystem::absolute(argv[3]);
        exercise(root, scene_path, worker);
        const auto saved = read(root / scene_path);
        const std::array roots{saved.at("asset_id").get<AssetId>()};
        const RuntimePackageTarget target{"windows", "d3d12"};
        package_runtime_content(root, scratch / "Export", roots, target);
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto catalog = open_runtime_content(scratch / "Relocated", target);
        exercise(scratch / "Relocated", catalog.records().at(roots.front()).source, worker);
        std::cout
            << "Gallery runtime floor/Agent rebake and source-hidden relocated content pass\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
