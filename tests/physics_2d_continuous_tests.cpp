#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json row(EntityId id, double x, unsigned motion) {
    return Json{
        {"id", id},
        {"name", "High-speed body"},
        {"spatial", {{"mode", "world"}}},
        {"components",
         {{"forge.local_translation", {{"x", x}, {"y", 0}, {"z", 0}}},
          {"forge.physics_body_2d", {{"motion", motion}, {"friction", 0}, {"restitution", 0}}}}}};
}
void crossing(unsigned shape, unsigned target_motion) {
    EngineContext engine(WorldRole::Runtime, false, {physics_module()});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    auto config = PhysicsConfig{};
    config.gravity = {};
    physics->configure(config);
    const auto scene_id = AssetId::generate();
    const auto actor = EntityId::generate(), target = EntityId::generate();
    auto moving = row(actor, -.8, 2), wall = row(target, 0, target_motion);
    moving["components"]["forge.physics_body_2d"]["bullet"] = true;
    auto& components = moving["components"];
    if (shape == 0)
        components["forge.box_collider_2d"] = {{"x", .1}, {"y", .1}};
    else if (shape == 1)
        components["forge.circle_collider_2d"] = {{"radius", .05}};
    else
        components["forge.capsule_collider_2d"] = {{"radius", .05}, {"height", .1}};
    wall["components"]["forge.box_collider_2d"] = {{"x", .05}, {"y", 3}};
    if (target_motion == 2) {
        // A massive non-bullet receiver is still a Dynamic native body.
        wall["components"]["forge.physics_body_2d"]["mass"] = 1000;
        wall["components"]["forge.physics_body_2d"]["lock_rotation"] = true;
    }
    scene.restore_snapshot(
        {{"version", 5}, {"asset_id", scene_id}, {"entities", Json::array({moving, wall})}});
    physics->synchronize(0);
    simulation.reset_presentation();
    physics->set_linear_velocity_2d({scene_id, actor}, {100, 0});
    for (unsigned tick = 0; tick < 4; ++tick) {
        simulation.tick(1.f / 60);
        const auto a = physics->body_state_2d({scene_id, actor});
        const auto b = physics->body_state_2d({scene_id, target});
        if (!a || !b || a->position[0] >= b->position[0] - .04 ||
            (tick == 0 && a->position[0] <= -.3) || std::abs(a->position[1]) >= .01)
            std::cerr << "Shape " << shape << ", receiver motion " << target_motion << ", tick "
                      << tick << ": actor " << (a ? a->position[0] : -999) << ','
                      << (a ? a->position[1] : -999) << ", receiver " << (b ? b->position[0] : -999)
                      << '\n';
        check(a && b && a->position[0] < b->position[0] - .04 &&
                  (tick != 0 || a->position[0] > -.3) && std::abs(a->position[1]) < .01,
              "High-speed 2D shape tunneled through the thin receiver");
    }
}
void continuous_sensors(unsigned count) {
    EngineContext engine(WorldRole::Runtime, false, {physics_module()});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    auto config = PhysicsConfig{};
    config.gravity = {};
    physics->configure(config);
    const auto actor = EntityId::generate();
    const auto scene_id = AssetId::generate();
    auto moving = row(actor, -.8, 2);
    moving["components"]["forge.physics_body_2d"]["bullet"] = true;
    moving["components"]["forge.circle_collider_2d"] = {{"radius", .05}};
    Json rows = Json::array({moving});
    for (unsigned i = 0; i < count; ++i) {
        auto strip = row(EntityId::generate(), -.35 + i * .1, 0);
        strip["components"]["forge.physics_body_2d"]["sensor"] = true;
        strip["components"]["forge.box_collider_2d"] = {{"x", .025}, {"y", 3}};
        rows.push_back(std::move(strip));
    }
    scene.restore_snapshot({{"version", 5}, {"asset_id", scene_id}, {"entities", rows}});
    physics->synchronize(0);
    simulation.reset_presentation();
    physics->set_linear_velocity_2d({scene_id, actor}, {100, 0});
    simulation.tick(1.f / 60);
    const auto state = physics->body_state_2d({scene_id, actor});
    check(state && state->position[0] > .8 && physics->contact_count_2d() == count,
          "Fast 2D sensor crossings blocked the visitor or lost native Begin observations");
    for (std::size_t i = 0; i < count; ++i) {
        const auto event = physics->contact_event_2d(i);
        check(event && event->sensor && event->begin && event->second == EntityRef{scene_id, actor},
              "Continuous sensor Begin lost its typed visitor");
    }
    simulation.tick(1.f / 60);
    check(physics->contact_count_2d() == count,
          "Continuous sensor crossing lost its next-tick End");
    for (std::size_t i = 0; i < count; ++i) {
        const auto event = physics->contact_event_2d(i);
        check(event && event->sensor && !event->begin &&
                  event->second == EntityRef{scene_id, actor},
              "Continuous sensor End lost its typed visitor");
    }
}

} // namespace
int main() {
    try {
        for (unsigned shape = 0; shape < 3; ++shape)
            for (unsigned motion = 0; motion < 3; ++motion)
                crossing(shape, motion);
        continuous_sensors(1);
        continuous_sensors(8);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
