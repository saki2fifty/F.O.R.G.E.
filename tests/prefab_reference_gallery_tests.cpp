#include "json_value_equal.hpp"
#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected, double epsilon = .0001) {
    if (std::abs(actual - expected) > epsilon)
        throw std::runtime_error("Portable Gallery oracle: " + std::to_string(actual) + " vs " +
                                 std::to_string(expected));
}
struct Actors {
    EntityRef left, right;
    std::map<std::string, EntityId> left_members, right_members;
    explicit Actors(const Json& source) {
        check(source.at("_prefab_sources").size() == 1, "Unexpected portable source closure");
        const auto& prefab = source.at("_prefab_sources")[0];
        check(prefab.at("version") == 3, "Gallery did not load a portable source3 prefab");
        for (const auto& row : source.at("entities"))
            if (row.contains("prefab_instance")) {
                const bool is_left = row.at("name").get<std::string>().starts_with("Left rig");
                const EntityRef reference{source.at("asset_id").get<AssetId>(),
                                          row.at("id").get<EntityId>()};
                if (is_left)
                    left = reference;
                else
                    right = reference;
                auto& members = is_left ? left_members : right_members;
                for (const auto& member : prefab.at("members"))
                    members.emplace(member.at("name").get<std::string>(),
                                    row.at("prefab_instance")
                                        .at("members")
                                        .at(member.at("id").get<std::string>())
                                        .get<EntityId>());
            }
        if (!(bool(left.entity) && bool(right.entity) && left != right)) {
            std::cerr << Json{{"left", left}, {"right", right}}.dump() << '\n';
            for (const auto& row : source.at("entities"))
                if (row.contains("prefab_instance"))
                    std::cerr << row.at("name") << ' ' << row.at("id") << '\n';
            throw std::runtime_error("Gallery requires independent left/right instances");
        }
    }
};
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    std::shared_ptr<PhysicsRuntime> physics;
    World(const std::filesystem::path& root, const Json& source, bool initialize_physics = true)
        : engine(WorldRole::Runtime, false, {physics_module({}, root), animation_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())),
          physics(std::static_pointer_cast<PhysicsRuntime>(engine.services().physics())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Portable Gallery admission stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (initialize_physics)
            physics->synchronize(0);
        simulation.reset_presentation();
    }
};
Double3 point(World& world, EntityId id) {
    world.engine.world().evaluate_world_transforms();
    return world.scene.entity(id.str()).get<WorldTransform>().affine.point({0, 0, 0});
}
void inspect_links(World& world, const Actors& actors) {
    for (const bool left : {true, false}) {
        const auto ref = left ? actors.left : actors.right;
        const auto& members = left ? actors.left_members : actors.right_members;
        const auto door = world.scene.entity(members.at("Sliding door").str());
        check(door.get<TransformAnimator>().sequence == ref,
              "Runtime track borrowed another instance controller");
        const auto joint =
            world.scene.entity(members.at("Internal distance link").str()).get<PhysicsJoint>();
        check(joint.body_a == EntityRef{ref.scene, members.at("Pendulum anchor")} &&
                  joint.body_b == EntityRef{ref.scene, members.at("Pendulum bob")},
              "Runtime constraint borrowed another instance body");
        const auto anchor = point(world, members.at("Pendulum anchor"));
        const auto bob = point(world, members.at("Pendulum bob"));
        double distance = 0;
        for (unsigned i = 0; i < 3; ++i)
            distance += (anchor[i] - bob[i]) * (anchor[i] - bob[i]);
        near(std::sqrt(distance), 2, .03);
    }
    check(world.physics->status().at("joints") == 2,
          "Native Jolt did not create both independent constraints");
}
void exercise(const std::filesystem::path& root, AssetId scene_id) {
    const auto source = load_game_scene(root, {scene_id});
    const Actors actors(source);
    World live(root, source);
    inspect_links(live, actors);
    const auto render = extract_render_scene(live.simulation.presentation(1));
    check(render.meshes.size() == 13 && render.cameras.size() == 1 && render.lights.size() == 1,
          "Portable Gallery render consumer lacks its complete scene");
    for (unsigned tick = 0; tick < 120; ++tick) {
        live.simulation.tick(1.f / 60);
        inspect_links(live, actors);
    }
    near(live.animation->playback(actors.left)->time, 2);
    near(live.animation->playback(actors.right)->time, 1);
    near(live.scene.entity(actors.left_members.at("Sliding door").str()).get<LocalTranslation>().x,
         1.375);
    near(live.scene.entity(actors.right_members.at("Sliding door").str()).get<LocalTranslation>().x,
         -1.375);
    const auto left_bob = point(live, actors.left_members.at("Pendulum bob"));
    const auto right_bob = point(live, actors.right_members.at("Pendulum bob"));
    check(std::abs(left_bob[1] - right_bob[1]) > .05,
          "Independent bob gravity overrides did not reach native physics");
    const auto snapshot = live.scene.snapshot();
    const auto animation = live.animation->checkpoint(), physics = live.physics->checkpoint();
    World restored(root, snapshot, false);
    restored.physics->restore(physics);
    restored.animation->restore(animation);
    restored.simulation.reset_presentation();
    inspect_links(restored, actors);
    for (unsigned tick = 0; tick < 15; ++tick) {
        live.simulation.tick(1.f / 60);
        restored.simulation.tick(1.f / 60);
        for (const auto& members : {actors.left_members, actors.right_members}) {
            const auto original = point(live, members.at("Pendulum bob"));
            const auto recovered = point(restored, members.at("Pendulum bob"));
            for (unsigned i = 0; i < 3; ++i)
                near(original[i], recovered[i], .001);
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--admit") {
            Json authored;
            std::ifstream(argv[3]) >> authored;
            EngineContext engine;
            Scene scene(engine.world());
            scene.restore_snapshot(
                load_game_scene(argv[2], {authored.at("asset_id").get<AssetId>()}));
            check(detail::json_value_equal(scene.document(), authored),
                  "Structured Gallery admission changed authored values or reference ownership");
            std::cout << "Structured Gallery scene admitted without changing authored intent\n";
            return 0;
        }
        check(argc == 3, "Provide Gallery root and isolated scratch directory");
        const auto scratch = std::filesystem::absolute(argv[2]);
        std::filesystem::remove_all(scratch);
        std::filesystem::create_directories(scratch);
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto catalog = AssetCatalog::open_project(root);
        AssetId scene;
        for (const auto& [id, record] : catalog.records())
            if (record.source == "Scenes/63-portable-prefab-rigs.scene.json")
                scene = id;
        check(bool(scene), "Missing original portable Gallery Scene63");
        exercise(root, scene);
        const std::array roots{scene};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        const auto destination = scratch / "export";
        package_runtime_content(root, destination, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "relocated";
        std::filesystem::rename(destination, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        check(shipped.records().size() == 3, "Portable Scene/prefab/clip closure is incorrect");
        for (const auto& [id, record] : shipped.records()) {
            (void)id;
            if (record.type == TransformAnimationAsset::type)
                check(!std::filesystem::exists(relocated / catalog.records().at(id).source) &&
                          std::filesystem::exists(relocated / record.source),
                      "Export did not replace the editable clip source with its selected cook");
        }
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(relocated, scene);
        std::cout << "Gallery63 portable sequence/Jolt instances, recovery and relocated "
                     "source-offline Windows-target data passed on this Linux host\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
