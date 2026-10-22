#pragma once
#include <chrono>
#include <cmath>
#include <forge/runtime_world.hpp>
#include <thread>

namespace forge::test {
// Throwaway capture source only. The checked Scene loader admits the result;
// neither the Gallery file nor its authored identities are rewritten on disk.
inline Json character_capture_source(Json source) {
    std::string selected;
    double offset = 0;
    for (auto& row : source.at("entities")) {
        auto& values = row.at("components");
        if (!values.contains("forge.capsule_collider_2d"))
            continue;
        if (!selected.empty())
            throw std::runtime_error("Character capture requires one selected capsule");
        selected = row.at("id").get<std::string>();
        const auto capsule = values.at("forge.capsule_collider_2d");
        offset = capsule.at("radius").get<double>() + .5 * capsule.at("height").get<double>();
        values.erase("forge.physics_body_2d");
        values.erase("forge.capsule_collider_2d");
        values["forge.character_controller_2d"] = capsule;
        values["forge.local_rotation"] = {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}};
        auto& y = values.at("forge.local_translation").at("y");
        y = y.get<double>() - offset;
        row["name"] = "Grounded character - feet origin / copied live feedback";
    }
    if (selected.empty())
        throw std::runtime_error("Character capture capsule missing");
    for (auto& row : source.at("entities"))
        if (row.contains("parent") && row.at("parent") == selected) {
            auto& y = row.at("components").at("forge.local_translation").at("y");
            y = y.get<double>() + offset;
        }
    return source;
}
// Use the same RuntimeWorld/physics owner as Play/export. Live capture data is
// observed after actual fixed movement and a genuinely blocked placement.
inline Json prepare_character_capture(RuntimeWorld& world, const Json& source) {
    world.scene.restore_snapshot(source);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Character capture runtime did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef character;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.character_controller_2d"))
            character = world.scene.reference(row.at("id").get<std::string>());
    world.physics()->move_character_2d(character, {0, -3});
    // Other Gallery bodies can temporarily support or obstruct the falling
    // character. Wait for observed ground instead of assuming an arrival tick.
    CharacterState2D before;
    bool grounded = false;
    for (unsigned tick = 0; tick < 600; ++tick) {
        world.simulation.tick(1.f / 60);
        if (world.physics()->character_2d(character).ground != CharacterGround::OnGround)
            continue;
        world.physics()->move_character_2d(character, {.5, -1});
        world.simulation.tick(1.f / 60);
        before = world.physics()->character_2d(character);
        if (before.ground == CharacterGround::OnGround && before.supporting_entity &&
            before.supporting_epoch && std::hypot(before.velocity[0], before.velocity[1]) > .1) {
            grounded = true;
            break;
        }
        world.physics()->move_character_2d(character, {0, -3});
    }
    if (!grounded)
        throw std::runtime_error(
            "Character capture lost actual floor support or clipped velocity: " +
            Json{{"position", before.position},
                 {"state", world.physics()->character_debug(character)}}
                .dump());
    world.physics()->place_character_2d(character, {-8, -4}, true);
    world.physics()->synchronize(0);
    const auto blocked = world.physics()->character_2d(character);
    if (!blocked.placement_blocked || blocked.position != before.position ||
        blocked.velocity != before.velocity)
        throw std::runtime_error("Character capture did not preserve a genuinely blocked pose");
    return {{"character_debug", world.physics()->character_debug(character)}};
}
} // namespace forge::test
