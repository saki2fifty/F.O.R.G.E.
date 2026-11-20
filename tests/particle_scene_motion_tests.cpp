#include "particle_scene_motion.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json box(const char* id, double x, double y, double height, unsigned layer, bool sensor) {
    return {{"id", id},
            {"name", id},
            {"components",
             {{"forge.position", {{"x", x}, {"y", y}, {"z", 0}}},
              {"forge.physics_body",
               {{"motion", 0},
                {"layer", layer},
                {"sensor", sensor},
                {"gravity_factor", 0},
                {"density", 1000},
                {"mass", 0},
                {"friction", .5},
                {"restitution", 0}}},
              {"forge.box_collider", {{"x", 20}, {"y", height}, {"z", 20}}}}}};
}
void force_cases(PhysicsService& physics, double offset) {
    const auto query = [&](const Double3& position, const Double3& delta, double radius) {
        PhysicsSweep sweep;
        sweep.origin = {position[0], position[1], position[2]};
        sweep.dimensions = {radius, 0, 0};
        sweep.displacement = delta;
        return physics.shape_cast(sweep, {1u << 1, false});
    };
    const auto clear = [](const auto&, const auto&, double) -> std::optional<PhysicsHit> {
        return {};
    };
    const auto rejected = [&](auto&& action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Scene force input was not rejected");
    };
    ParticleSceneState initial{{{offset, 3, 0}, {0, -120, 0}}};
    const Double3 gravity{0, -9.81, 0};
    for (double drag : {0., 1e-8, 2., 64.}) {
        const auto step = advance_particle_scene_motion(initial, gravity, drag, .1, {}, query);
        check(step.state.alive && step.state.bounces <= 1 &&
                  step.state.motion.position[1] >= .049 && step.queries <= 128,
              "Native force/drag sweep tunneled or exceeded its bounded workload");
        const auto unconstrained =
            advance_particle_scene_motion(initial, gravity, drag, .1, {}, clear);
        const auto exact = evaluate_particle_motion(initial.motion, gravity, drag, .1);
        for (unsigned axis = 0; axis < 3; ++axis)
            check(std::abs(unconstrained.state.motion.position[axis] - exact.position[axis]) <
                          1e-6 &&
                      std::abs(unconstrained.state.motion.velocity[axis] - exact.velocity[axis]) <
                          1e-8,
                  "Force subdivision changed clear-space analytic motion");
        for (unsigned sample_index = 0; sample_index <= 100; ++sample_index) {
            const auto value = sample_particle_scene_motion(step, .001 * sample_index);
            check(value && value->position[1] >= .049,
                  "Force contact interpolation crossed the tested thin floor");
        }
    }
    // Both interval endpoints lie above the floor; the curved excursion crosses it.
    ParticleSceneState turning{{{offset, .2, 0}, {0, -40, 0}}};
    auto no_bounces = ParticleSceneCollision{};
    no_bounces.bounce_limit = 0;
    const auto turned =
        advance_particle_scene_motion(turning, {0, 4000, 0}, 0, .02, no_bounces, query);
    check(!turned.state.alive && turned.death && *turned.death > 0 && *turned.death < .01 &&
              sample_particle_scene_motion(turned, 0) && !sample_particle_scene_motion(turned, .02),
          "Curved contact missed the interior excursion or retained a dead birth");
    ParticleSceneState touching{{{offset, .05, 0}, {0, 0, 0}}};
    const auto resting = advance_particle_scene_motion(touching, gravity, 0, .01, {}, query);
    check(!resting.state.alive && resting.death == 0,
          "Sustained inward-force contact invented a resting-body solver");
    ParticleSceneState outgoing{{{offset, .05, 0}, {0, 5, 0}}};
    const auto departing = advance_particle_scene_motion(outgoing, gravity, 2, .1, {}, query);
    check(departing.state.alive && departing.state.bounces == 0 &&
              departing.state.motion.position[1] > .4,
          "Departing force contact retained its initial touching surface");
    auto absorbing = ParticleSceneCollision{};
    absorbing.restitution = 0;
    const auto absorbed = advance_particle_scene_motion(initial, gravity, 0, .1, absorbing, query);
    check(!absorbed.state.alive && absorbed.death && absorbed.state.bounces == 1,
          "Zero-rebound inward-force contact did not end at the first impact");
    auto rough = ParticleSceneCollision{};
    rough.friction = .75;
    initial.motion.velocity[0] = 6;
    const auto bounced = advance_particle_scene_motion(initial, {0, 0, 0}, 0, .1, rough, query);
    check(bounced.state.bounces == 1 && bounced.state.motion.velocity[0] == 1.5 &&
              bounced.state.motion.velocity[1] == 60,
          "Scene contact did not separate normal restitution and tangent loss");
    ParticleSceneState stationary{{{offset, 4, 0}, {0, 0, 0}}};
    const auto stopped = advance_particle_scene_motion(stationary, {0, 0, 0}, 0, .1, {}, query);
    check(stopped.state == stationary && stopped.queries == 1,
          "Zero-displacement native sweep changed a clear stationary particle");
    for (unsigned kind = 0; kind < 7; ++kind) {
        auto invalid = ParticleSceneCollision{};
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        if (kind == 0)
            invalid.radius = nan;
        if (kind == 1)
            invalid.restitution = nan;
        if (kind == 2)
            invalid.friction = nan;
        if (kind == 3)
            invalid.bounce_limit = 17;
        rejected([&] {
            (void)advance_particle_scene_motion(initial, kind == 4 ? Double3{nan, 0, 0} : gravity,
                                                kind == 5 ? nan : 0, kind == 6 ? nan : .1, invalid,
                                                query);
        });
    }
    const auto retained = initial;
    unsigned calls = 0;
    rejected([&] {
        (void)advance_particle_scene_motion(
            initial, gravity, 0, .1, {},
            [&](const auto& position, const auto& delta, double radius) {
                if (++calls == 2)
                    throw std::runtime_error("Injected native query failure");
                return query(position, delta, radius);
            });
    });
    check(initial == retained, "Force query failure mutated its admitted input state");
    rejected(
        [&] { (void)advance_particle_scene_motion(initial, {0, 10000, 0}, 0, 30, {}, clear); });
    for (double radius : {.001, .05, 1., 1000.}) {
        auto settings = ParticleSceneCollision{};
        settings.radius = radius;
        ParticleSceneState sized{{{offset, radius + 1, 0}, {0, -120, 0}}};
        const auto contact =
            advance_particle_scene_motion(sized, {0, 0, 0}, 0, .02, settings, query);
        check(contact.state.alive && contact.state.bounces == 1 &&
                  contact.state.motion.position[1] >= radius &&
                  std::abs(contact.state.motion.position[1] - (radius + .7 + radius * 1e-4)) <
                      1e-4 * std::max(1., radius) &&
                  contact.state.motion.velocity[1] == 60,
              "Native Scene radius boundary lost thin-floor contact/remainder motion");
        sized.motion.position[1] = radius;
        sized.motion.velocity[1] = 1;
        const auto departure =
            advance_particle_scene_motion(sized, {0, 0, 0}, 0, .1, settings, query);
        check(departure.state.alive && departure.state.bounces == 0 &&
                  departure.state.motion.position[1] >= radius + .09 && departure.queries <= 3,
              "Native Scene radius boundary misclassified a departing touch");
    }
}

void settings_codec() {
    ParticleSceneCollision settings{.025, .75, .25, 16, 0x80000001u};
    const auto encoded = encode_particle_scene_collision(settings);
    check(decode_particle_scene_collision(encoded) == settings,
          "Scene collision settings lost metric values or high layer bits");
    for (const auto* name : {"radius", "restitution", "friction", "bounce_limit", "mask"}) {
        auto missing = encoded;
        missing.erase(name);
        bool rejected = false;
        try {
            (void)decode_particle_scene_collision(missing);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Incomplete Scene collision settings were admitted");
    }
    for (unsigned kind = 0; kind < 9; ++kind) {
        auto invalid = encoded;
        if (kind == 0)
            invalid["mask"] = -1;
        if (kind == 1)
            invalid["mask"] = std::uint64_t{UINT32_MAX} + 1;
        if (kind == 2)
            invalid["mask"] = 1.5;
        if (kind == 3)
            invalid["bounce_limit"] = -1;
        if (kind == 4)
            invalid["bounce_limit"] = 17;
        if (kind == 5)
            invalid["radius"] = 0;
        if (kind == 6)
            invalid["radius"] = true;
        if (kind == 7)
            invalid["extra"] = 1;
        if (kind == 8)
            invalid["friction"] = 1.1;
        bool rejected = false;
        try {
            (void)decode_particle_scene_collision(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid Scene collision settings were admitted");
    }
}
} // namespace
int main() {
    try {
        settings_codec();
        for (double offset : {0., 1e8, -1e8}) {
            EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
            Scene scene(engine.world());
            auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
            PhysicsConfig config;
            config.layers[1] = "Environment";
            physics->configure(config);
            scene.restore_snapshot(
                {{"version", 1},
                 {"entities", Json::array({box("floor", offset, -.005, .01, 1, false)})}});
            physics->synchronize(0);
            force_cases(*physics, offset);
        }
        std::cout << "Scene particle native motion and settings passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
