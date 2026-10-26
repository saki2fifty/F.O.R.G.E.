#pragma once
#include "runtime_tile_map.hpp"
#include <forge/game_content.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_world.hpp>
#include <set>
#include <thread>

inline void test_moving_trigger_gallery(const std::filesystem::path& root, forge::AssetId scene_id,
                                        bool moving) {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    const auto source = load_game_scene(root, {scene_id});
    world.scene.restore_snapshot(source);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Moving/trigger Gallery resources did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto physics = world.physics();
    EntityRef map;
    std::vector<std::pair<EntityRef, double>> visitors;
    for (const auto& row : source.at("entities")) {
        const auto entity = world.scene.entity(row.at("id").get<std::string>());
        const auto ref = world.scene.reference(row.at("id").get<std::string>());
        if (entity.has<TileMapCollider2D>()) {
            map = ref;
            const auto body = entity.get<PhysicsBody2D>();
            check(body.motion == (moving ? 1u : 0u) && body.sensor == !moving,
                  "Gallery Map lost its intended Kinematic/trigger configuration");
        } else if (entity.has<PhysicsBody2D>() && entity.get<PhysicsBody2D>().motion == 2) {
            const auto half = entity.has<CircleCollider2D>()
                                  ? double(entity.get<CircleCollider2D>().radius)
                                  : .5 * entity.get<BoxCollider2D>().y;
            visitors.emplace_back(ref, -5.25 + half);
        }
    }
    check(map.entity && visitors.size() == 4, "Moving/trigger Gallery participants are missing");
    const auto epoch = physics->body_state_2d(map)->epoch;
    const auto cells = detail::runtime_tile_map_snapshot(world.engine.world(), map);
    std::set<EntityRef> entered, exited;
    double left = 10, right = -10;
    for (unsigned tick = 0; tick < 240; ++tick) {
        world.simulation.tick(1.f / 60);
        const auto body = physics->body_state_2d(map);
        check(body && body->epoch == epoch &&
                  detail::runtime_tile_map_snapshot(world.engine.world(), map) == cells,
              "Gallery Map replaced unchanged geometry/cells during ordinary movement");
        left = std::min(left, body->position[0]);
        right = std::max(right, body->position[0]);
        for (std::size_t i = 0; i < physics->contact_count_2d(); ++i) {
            const auto event = physics->contact_event_2d(i);
            if (event && event->sensor && event->first == map) {
                check(!moving && event->first_available && event->first_epoch == epoch,
                      "Gallery sensor event has a stale Map binding");
                (event->begin ? entered : exited).insert(event->second);
            }
        }
    }
    if (moving)
        check(left < -2.9 && right > 2.9,
              "Gallery painted course did not follow its ordinary transform animation");
    else {
        check(entered.size() == 4 && exited.size() == 4,
              "Gallery trigger layer did not report each passing visitor's Begin/End");
        for (const auto& [ref, expected] : visitors) {
            const auto actor = physics->body_state_2d(ref);
            check(actor && std::abs(actor->position[1] - expected) < .025,
                  "Gallery visitor stopped on a trigger or missed the solid receiver");
        }
    }
    const auto source_after = load_game_scene(root, {scene_id});
    check(source_after == source, "Moving/trigger Gallery gameplay wrote the saved Scene");
}
