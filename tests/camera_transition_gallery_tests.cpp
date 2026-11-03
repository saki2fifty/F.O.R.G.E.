#include "animation_playback_access.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/153-camera-transitions.scene.json";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Camera transition Gallery already exists");
    std::filesystem::create_directories(root / "Assets/CameraTransitions");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto motion =
        TransformAnimationDocument::create(lease, "Assets/CameraTransitions/Rig.animation.json");
    motion->edit(motion->revision(), "Six-second shared camera rig", [](Json& json) {
        TransformAnimationSource source{json};
        source.retime_duration(6 * animation_timebase);
        source.document["channels"] = {"forge.local_translation"};
        source.set_key(0, {});
        source.set_key(3 * animation_timebase, {{.6, 0, 0}, {}, {1, 1, 1}});
        source.set_key(6 * animation_timebase, {});
        json = source.document;
    });
    motion->save();
    AssetImportService imports(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(motion->locator(), {}, motion->source().asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "Camera rig publication timed out");
    const auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes.front().published, "Camera rig publication failed");
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe = "empty") {
        return authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto director = create("Director - six-second loop", "animation.sequence");
    const EntityRef sequence{scene.asset_id(), EntityId::parse(director)};
    auto config = scene.entity(director).get<AnimationSequence>();
    config.duration = 6;
    config.loop = true;
    scene.entity(director).set(config);
    const auto rig = create("Shared camera rig - animated translation");
    TransformAnimator player{{motion->source().asset()}};
    player.sequence = sequence;
    scene.entity(rig).set(player);
    scene.reparent_entity(rig, director, ReparentMode::KeepLocal);
    const auto camera = [&](const char* name, Double3 position, double fov, bool fallback) {
        const auto id = create(name, "render.camera");
        Camera lens;
        lens.enabled = fallback;
        lens.vertical_fov = fov;
        lens.background_r = .025f;
        lens.background_g = .04f;
        lens.background_b = .07f;
        scene.entity(id).set(lens).set<LocalTranslation>({position[0], position[1], position[2]});
        if (!fallback)
            scene.reparent_entity(id, rig, ReparentMode::KeepLocal);
        return id;
    };
    const auto a = camera("Camera A - left / narrow", {-2, .7, -7}, .9, false);
    const auto b = camera("Camera B - right / wide", {2, 1, -5}, 1.2, false);
    camera("Fallback - final half-second overview", {0, 1, -10}, 1, true);
    const auto shot = [&](const char* name, const std::string& target, double start, double end,
                          double blend) {
        const auto id = create(name, "animation.camera_shot");
        scene.entity(id).set<CameraShot>(
            {sequence, {scene.asset_id(), EntityId::parse(target)}, start, end, true, blend});
        scene.reparent_entity(id, director, ReparentMode::KeepLocal);
    };
    shot("A - initial cut", a, 0, 2, 0);
    shot("B - one-second smooth incoming view", b, 2, 4, 1);
    shot("A - one-second smooth return", a, 4, 5.5, 1);
    for (unsigned i = 0; i < 5; ++i) {
        const auto id = create(("Depth marker " + std::to_string(i + 1)).c_str());
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(0);
        mesh.material_parameters = {
            {"surface",
             "baseColorFactor",
             {MaterialParameterType::LinearColor4,
              std::array<float, 4>{.1f + .17f * i, .8f - .13f * i, .2f + .14f * i, 1}}}};
        scene.entity(id).set(mesh).set<LocalTranslation>({-3. + 1.5 * i, 0, i % 2 ? 5. : 3.});
    }
    const auto floor = create("Floor - depth reference");
    MeshRenderer mesh;
    mesh.mesh = engine_primitive(0);
    scene.entity(floor).set(mesh).set<LocalTranslation>({0, -1, 4}).set<LocalScale>({10, .2, 8});
    create("Directional light", "render.light");
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Camera rig admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef controller;
    EntityId incoming;
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        const auto actor = scene.entity(id);
        if (actor.has<AnimationSequence>())
            controller = {scene.asset_id(), EntityId::parse(id)};
        if (actor.has<CameraShot>() && actor.get<CameraShot>().start_seconds == 2)
            incoming = actor.get<CameraShot>().camera.entity;
    }
    check(bool(controller.entity) && bool(incoming), "Camera transition Gallery tracks missing");
    for (double time : {2., 2.25, 2.5, 2.75, 3.}) {
        AnimationPlaybackAccess::control(*animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        const auto render = extract_render_scene(simulation.presentation(1));
        const double linear = time - 2, weight = linear * linear * (3 - 2 * linear);
        check(render.cameras.size() == 1 && render.cameras[0].entity == incoming,
              "Animated transition lost the incoming Camera identity");
        const auto rig_x = time * .2;
        check(std::abs(render.cameras[0].world.m[3] - (rig_x - 2 + 4 * weight)) < 1e-5 &&
                  std::abs(render.cameras[0].camera.vertical_fov - std::lerp(.9, 1.2, weight)) <
                      1e-6,
              "Animated world-space endpoints or lens blend differ");
        check(scene.entity(incoming.str()).get<LocalTranslation>().x == 2,
              "Gallery transition wrote native Camera translation");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root, or --author and Gallery root");
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2]);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / scene_path) >> source;
        exercise(root, source);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto package = scratch / "export";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(package, relocated);
        const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
        check(catalog.records().size() == 2, "Camera transition closure differs");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        exercise(relocated, load_game_scene(relocated, {roots[0]}));
        std::cout << "Original animated camera-transition Gallery and relocated two-asset closure "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
