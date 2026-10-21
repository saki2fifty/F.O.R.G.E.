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
            const std::array roots{scene};
            prepare_runtime_content_catalog(*lease, roots, {}, schema.schema());
        }
        catalog = AssetCatalog::open_project(root);
        playback(root, catalog, scene);
        const auto package = scratch / "prepared content";
        const std::array roots{scene};
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
