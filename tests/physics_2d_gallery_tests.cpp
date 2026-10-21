#include "material_selection.hpp"
#include "runtime_package.hpp"
#include "sprite_material_profile.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <map>
#include <set>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
AssetId asset(const AssetCatalog& catalog, std::string_view source) {
    for (const auto& [id, row] : catalog.records())
        if (!row.subasset && path_utf8(row.source) == source)
            return id;
    throw std::runtime_error("2D Gallery asset missing: " + std::string(source));
}
void connected_playback(const std::filesystem::path& root, AssetId scene_id) {
    EngineContext engine(WorldRole::Runtime, false, {physics_module()});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    physics->synchronize(0);
    simulation.reset_presentation();
    std::map<std::string, EntityRef> refs;
    unsigned joints = 0;
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities")) {
        refs.emplace(row.at("name").get<std::string>(),
                     EntityRef{scene_id, row.at("id").get<EntityId>()});
        joints += row.at("components").contains("forge.physics_joint_2d");
    }
    const auto state = [&](const char* name) {
        const auto value = physics->body_state_2d(refs.at(name));
        check(bool(value), "Connected 2D Gallery body missing");
        return *value;
    };
    check(joints == 4 && physics->status().at("joints_2d") == 4 &&
              physics->status().at("bodies_2d") == 8,
          "Connected Gallery lost its four joints or body pairs");
    double pendulum_left = -4, spring_top = -1;
    for (unsigned tick = 0; tick < 480; ++tick) {
        simulation.tick(1.f / 60);
        const auto hinge = state("Cyan pendulum"), distance = state("Amber tethered body"),
                   spring = state("Rose spring body"), slider = state("Green motor carriage");
        // Independent world geometry: the hinge bob's local (-2,0) is its pivot.
        check(std::hypot(hinge.position[0] - 2 * std::cos(hinge.angle) + 6,
                         hinge.position[1] - 2 * std::sin(hinge.angle) - 2) < .003,
              "Gallery pendulum detached from its authored pivot");
        check(std::abs(std::hypot(distance.position[0] + 2, distance.position[1] - 2) - 2) < .005,
              "Gallery rigid tether changed its two metre distance");
        check(std::abs(slider.position[1] - 1) < .005 && slider.position[0] <= 7.51 &&
                  slider.position[0] >= 5.99,
              "Gallery motor carriage left its limited rail");
        pendulum_left = std::min(pendulum_left, hinge.position[0]);
        spring_top = std::max(spring_top, spring.position[1]);
        const auto frame = extract_render_scene(simulation.presentation(1));
        check(frame.diagnostics.empty() && frame.cameras.size() == 1 &&
                  frame.sprites.size() == 14 && frame.meshes.empty(),
              "Connected Gallery lost its Sprite camera/presentation");
        const auto bob = scene.entity(refs.at("Cyan pendulum").entity.str());
        const auto arm = scene.entity(refs.at("Pendulum arm - follows the bob").entity.str());
        const auto expected = bob.get<WorldTransform>().affine.point({-1, 0, .02});
        const auto actual = arm.get<WorldTransform>().affine.point({0, 0, 0});
        check(std::hypot(expected[0] - actual[0], expected[1] - actual[1]) < 1e-5,
              "Gallery arm did not follow the ordinary visual hierarchy");
    }
    check(pendulum_left < -7 && spring_top > .3 &&
              std::abs(state("Rose spring body").position[1]) < .02 &&
              std::abs(state("Green motor carriage").position[0] - 7.5) < .01,
          "Connected Gallery did not demonstrate pendulum, elastic rest and limited motor");
}
void playback(const std::filesystem::path& root, const AssetCatalog& catalog, AssetId scene_id) {
    EngineContext engine(WorldRole::Runtime, false, {physics_module(), animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    const auto animation = animation_runtime(engine.world());
    animation->catalog(std::make_shared<const AssetCatalog>(catalog));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "2D Gallery clip loading timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    physics->synchronize(0);
    simulation.reset_presentation();
    std::map<EntityRef, double> initial;
    EntityRef floor, platform, sensor, capsule;
    std::vector<EntityId> caps;
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities")) {
        const auto id = row.at("id").get<EntityId>();
        const EntityRef ref{scene_id, id};
        const auto& components = row.at("components");
        if (components.contains("forge.physics_body_2d")) {
            const auto state = physics->body_state_2d(ref);
            check(bool(state) && state->epoch, "2D Gallery body was not realized");
            if (state->motion == 2)
                initial.emplace(ref, state->position[1]);
            else if (state->motion == 1)
                platform = ref;
            if (components.at("forge.physics_body_2d").at("sensor") == true)
                sensor = ref;
            if (row.at("name") == "Static floor")
                floor = ref;
            if (components.contains("forge.capsule_collider_2d"))
                capsule = ref;
        }
        if (row.contains("parent"))
            caps.push_back(id);
    }
    check(initial.size() == 5 && floor.entity && platform.entity && sensor.entity &&
              capsule.entity && caps.size() == 3,
          "2D Gallery lost its box/circle/capsule/platform/sensor participants");
    const auto with_sensor = physics->raycast_2d({6, 3}, {0, -8}, {UINT32_MAX, true});
    const auto solid_only = physics->raycast_2d({6, 3}, {0, -8}, {UINT32_MAX, false});
    check(with_sensor && with_sensor->entity == sensor && solid_only && solid_only->entity == floor,
          "2D Gallery sensor/solid ray filtering differs");
    std::set<EntityRef> fallen, entered, exited;
    bool solid_contact = false;
    double left = 10, right = -10;
    for (unsigned tick = 0; tick < 480; ++tick) {
        simulation.tick(1.f / 60);
        const auto moving = physics->body_state_2d(platform);
        check(moving && moving->motion == 1 && std::abs(moving->position[1] + 2) < 1e-4,
              "2D Gallery platform changed its planar height or motion mode");
        left = std::min(left, moving->position[0]);
        right = std::max(right, moving->position[0]);
        for (const auto& [ref, y] : initial) {
            const auto state = physics->body_state_2d(ref);
            check(state && std::isfinite(state->position[0]) && std::isfinite(state->angle) &&
                      state->position[1] > -4.1 && std::abs(state->position[0]) < 8,
                  "2D Gallery Dynamic escaped its static enclosure");
            if (state->position[1] < y - .5)
                fallen.insert(ref);
        }
        for (std::size_t i = 0; i < physics->contact_count_2d(); ++i) {
            const auto event = physics->contact_event_2d(i);
            check(event && event->first_available && event->second_available,
                  "2D Gallery contact lost its current body references");
            if (event->sensor && event->first == sensor && initial.contains(event->second))
                (event->begin ? entered : exited).insert(event->second);
            solid_contact |= !event->sensor && event->begin;
        }
        const auto rendered = extract_render_scene(simulation.presentation(1));
        check(rendered.diagnostics.empty() && rendered.cameras.size() == 1 &&
                  rendered.sprites.size() == 12 && rendered.meshes.empty(),
              "2D Gallery lost its ordinary Sprite presentation");
        const auto transform = scene.entity(capsule.entity.str()).get<WorldTransform>().affine;
        for (const auto& sprite : rendered.sprites)
            if (std::find(caps.begin(), caps.end(), sprite.entity) != caps.end()) {
                const auto local = scene.entity(sprite.entity.str()).get<LocalTranslation>();
                const auto expected = transform.point({local.x, local.y, local.z});
                const auto actual = sprite.world.point({0, 0, 0});
                check(std::hypot(expected[0] - actual[0], expected[1] - actual[1]) < 1e-5,
                      "2D Gallery visual child stopped following its Dynamic body");
            }
    }
    check(left < -2.9 && right > 2.9 && solid_contact && fallen.size() == 5 &&
              entered.size() == 5 && exited.size() == 5,
          "2D Gallery did not demonstrate falling, platform travel, solids and sensor crossings");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        } cleanup{scratch};
        const auto root = scratch / "authored Gallery";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(std::filesystem::absolute(argv[1]), root,
                              std::filesystem::copy_options::recursive);
        auto catalog = AssetCatalog::open_project(root);
        const auto scene = asset(catalog, "Scenes/75-2d-physics.scene.json");
        const auto connected = asset(catalog, "Scenes/76-connected-2d-bodies.scene.json");
        const auto solid = asset(catalog, "Assets/Physics2D/Solid.material.json");
        const auto badge = asset(catalog, "Assets/Sprites/Badge.material.json");
        const auto clip = asset(catalog, "Assets/Physics2D/Platform.animation.json");
        const auto texture = asset(catalog, "Assets/Sprites/Badge.png");
        for (const auto id : {solid, badge})
            validate_sprite_material_profile(
                asset_detail::load_material_selection(root, catalog, {id}).data.values);
        {
            const auto lease = std::make_shared<ProjectLease>(root);
            EngineContext author(WorldRole::Authoring, false, {physics_module()});
            Scene schema(author.world());
            const std::array roots{scene, connected};
            prepare_runtime_content_catalog(*lease, roots, {}, schema.schema());
        }
        catalog = AssetCatalog::open_project(root);
        playback(root, catalog, scene);
        connected_playback(root, connected);
        const auto package = scratch / "prepared content";
        const std::array roots{scene};
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto connected_package = scratch / "connected prepared content";
        const std::array connected_roots{connected};
        package_runtime_content(root, connected_package, connected_roots, {"windows", "d3d12"});
        std::filesystem::remove_all(root);
        const auto moved = scratch / "relocated source-free content";
        std::filesystem::rename(package, moved);
        const auto packed = open_runtime_content(moved, {"windows", "d3d12"});
        const std::set<AssetId> expected{scene, solid, badge, clip, texture};
        std::set<AssetId> actual;
        for (const auto& [id, record] : packed.records())
            actual.insert(id);
        check(actual == expected, "2D Gallery package lost its closure or pulled unrelated assets");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(moved)) {
            const auto name = path_utf8(entry.path().filename());
            check(!name.ends_with(".png") && !name.ends_with(".material.json") &&
                      !name.ends_with(".animation.json") && !name.ends_with(".cpp") &&
                      !name.ends_with(".hpp"),
                  "2D Gallery runtime package leaked authored assets or C++ source");
        }
        playback(moved, packed, scene);
        const auto connected_moved = scratch / "connected relocated source-free content";
        std::filesystem::rename(connected_package, connected_moved);
        const auto connected_packed = open_runtime_content(connected_moved, {"windows", "d3d12"});
        std::set<AssetId> connected_actual;
        for (const auto& [id, record] : connected_packed.records())
            connected_actual.insert(id);
        check(connected_actual == std::set<AssetId>{connected, solid},
              "Connected Gallery source-free package lost its exact closure");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(connected_moved)) {
            const auto name = path_utf8(entry.path().filename());
            check(!name.ends_with(".material.json") && !name.ends_with(".cpp") &&
                      !name.ends_with(".hpp"),
                  "Connected Gallery package leaked authored source");
        }
        connected_playback(connected_moved, connected);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
