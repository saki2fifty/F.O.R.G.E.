#include "animation_playback_access.hpp"
#include "runtime_package.hpp"
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
const char* scene_path(bool following) {
    return following ? "Scenes/220-camera-follow.scene.json" : "Scenes/219-camera-aim.scene.json";
}
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >= 1e-5)
        throw std::runtime_error("Camera Aim Gallery oracle: " + std::to_string(actual) + " vs " +
                                 std::to_string(expected));
}
void author(const std::filesystem::path& root, bool following) {
    check(!std::filesystem::exists(root / scene_path(following)),
          "Camera target Gallery already exists");
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe = "empty") {
        return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    };
    const auto director = create("Director - two-second target loop", "animation.sequence");
    scene.entity(director).set<AnimationSequence>({2, true, true, true});
    const EntityRef sequence{scene.asset_id(), EntityId::parse(director)};
    const auto target = create("Yellow target - animated left to right");
    MeshRenderer mesh;
    mesh.mesh = engine_primitive(0);
    mesh.material_parameters = {
        {"surface",
         "baseColorFactor",
         {MaterialParameterType::LinearColor4, std::array<float, 4>{1, .65f, .03f, 1}}}};
    TransformAnimator motion{{AssetId::parse("a5fa9266-52e5-48da-9771-86cb37294118")}};
    motion.sequence = sequence;
    scene.entity(target).set(mesh).set(motion).set<LocalTranslation>({-2, 1.5, 0});
    const auto camera = create(following ? "Following Camera - derived eye and aim"
                                         : "Tracking Camera - view correction only",
                               "render.camera");
    scene.entity(camera)
        .set<LocalTranslation>({0, 3, -8})
        .set<LocalRotation>({})
        .set<CameraAim>({true, {scene.asset_id(), EntityId::parse(target)}, 0, .5});
    if (following) {
        CameraFollow follow;
        follow.target = {scene.asset_id(), EntityId::parse(target)};
        scene.entity(camera).set(follow);
    }
    for (int index = -2; index <= 2; ++index) {
        const auto id = create(("Blue reference " + std::to_string(index + 3)).c_str());
        mesh.material_parameters[0].parameter.value = std::array<float, 4>{.06f, .3f, .8f, 1};
        scene.entity(id).set(mesh).set<LocalTranslation>({index * 2., .5, 3});
    }
    const auto floor = create("Floor - stationary reference");
    mesh.material_parameters[0].parameter.value = std::array<float, 4>{.2f, .25f, .3f, 1};
    scene.entity(floor).set(mesh).set<LocalTranslation>({0, -.15, 1}).set<LocalScale>({14, .2, 10});
    create("Directional light", "render.light");
    scene.save(root / scene_path(following));
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path(following));
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    const ProjectLease lease(root);
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
}
Json exercise(const std::filesystem::path& root, const Json& source, const Json& recovery = {}) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Camera Aim target admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef sequence;
    std::string camera;
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        if (scene.entity(id).has<AnimationSequence>())
            sequence = {scene.asset_id(), EntityId::parse(id)};
        if (scene.entity(id).has<CameraAim>())
            camera = id;
    }
    check(bool(sequence.entity) && !camera.empty(), "Camera Aim Gallery lost its actors");
    const auto original_camera = scene.entity(camera).get<LocalRotation>();
    const bool following = scene.entity(camera).has<CameraFollow>();
    if (!recovery.is_null()) {
        animation->restore(recovery);
        check(animation->checkpoint() == recovery, "Camera Aim recovery changed its shared clock");
    }
    for (double time : {0., .25, .5, .75, 1., 1.5, 1.875}) {
        AnimationPlaybackAccess::control(*animation, sequence,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        const auto render = extract_render_scene(simulation.presentation(1));
        check(render.diagnostics.empty(), "Camera Aim Gallery emitted a render diagnostic");
        const auto prepared = prepare_game_cameras(render, 960, 540);
        check(prepared.cameras.size() == 1, "Camera Aim Gallery lost its main view");
        const auto& view = prepared.cameras[0].view;
        // SlidingDoor uses a cubic segment with zero endpoint translation tangents.
        const auto u = time / 2;
        const auto x = -2 + 4 * u * u * (3 - 2 * u);
        const auto distance = following ? std::sqrt(38.25) : std::sqrt(x * x + 65);
        near(view.position[0], following ? x : 0);
        near(view.position[1], following ? 3.5 : 3);
        near(view.position[2], following ? -6 : -8);
        near(view.forward[0], following ? 0 : x / distance);
        near(view.forward[1], (following ? -1.5 : -1) / distance);
        near(view.forward[2], (following ? 6 : 8) / distance);
        check(scene.entity(camera).get<LocalRotation>() == original_camera,
              "Camera Aim Gallery changed native camera rotation");
        const auto native_eye = scene.entity(camera).get<LocalTranslation>();
        check(native_eye == LocalTranslation{0, 3, -8},
              "Camera following changed native camera position");
    }
    return animation->checkpoint();
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool following = argc == 4 && std::string_view(argv[3]) == "--follow";
        check(argc == 3 || following,
              "Need Gallery and scratch root, or --author and Gallery root; optional --follow");
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2], following);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / scene_path(following)) >> source;
        const auto checkpoint = exercise(root, source);
        exercise(root, source, checkpoint);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto package = scratch / "export";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(package, relocated);
        check(open_runtime_content(relocated, {"windows", "d3d12"}).records().size() == 2,
              "Camera Aim Gallery closure must contain its Scene and reused animation");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        exercise(relocated, load_game_scene(relocated, {roots[0]}));
        std::cout
            << "Camera target animated view, native pose isolation, recovery and source-hidden "
               "relocated two-asset Gallery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
