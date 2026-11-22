#include "particle_scene_motion.hpp"
#include "physics_2d.hpp"
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
void xy_cases(double offset) {
    EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
    Scene scene(engine.world());
    auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    PhysicsConfig config;
    config.layers[1] = "Environment";
    physics->configure(config);
    scene.restore_snapshot(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "xy-floor"},
                        {"name", "XY floor"},
                        {"components",
                         {{"forge.position", {{"x", offset}, {"y", offset - .005}, {"z", 0}}},
                          {"forge.physics_body_2d", {{"motion", 0}, {"layer", 1}}},
                          {"forge.box_collider_2d", {{"x", 20}, {"y", .01}}}}}}})}});
    physics->synchronize(0);
    const auto query = [&](const Double3& position, const Double3& delta,
                           double radius) -> std::optional<PhysicsHit> {
        PhysicsSweep2D sweep;
        sweep.origin = {position[0], position[1]};
        sweep.dimensions = {radius, 0};
        sweep.displacement = {delta[0], delta[1]};
        const auto hit = physics->shape_cast_2d(sweep, {1u << 1, false});
        if (!hit)
            return {};
        return PhysicsHit{
            hit->entity,
            {hit->position[0], hit->position[1], position[2] + delta[2] * hit->fraction},
            {hit->normal[0], hit->normal[1], 0},
            hit->fraction};
    };
    ParticleSceneCollision settings;
    settings.friction = .75;
    ParticleSceneState falling{{{offset, offset + 3, 1e8}, {6, -120, 7}}};
    const Double3 force{0, 0, 4000};
    const auto bounced =
        advance_particle_scene_motion(falling, force, 0, .1, settings, query, false, true);
    const auto depth = evaluate_particle_motion(falling.motion, force, 0, .1);
    if (!bounced.state.alive || bounced.state.bounces != 1 || bounced.queries > 2 ||
        bounced.state.motion.velocity[0] != 1.5 || bounced.state.motion.velocity[1] != 60)
        std::cerr << Json{{"offset", offset},
                          {"alive", bounced.state.alive},
                          {"bounces", bounced.state.bounces},
                          {"queries", bounced.queries},
                          {"position", bounced.state.motion.position},
                          {"velocity", bounced.state.motion.velocity}}
                         .dump()
                  << '\n';
    check(bounced.state.alive && bounced.state.bounces == 1 && bounced.queries <= 2 &&
              bounced.state.motion.position[1] >= offset + settings.radius &&
              bounced.state.motion.velocity[0] == 1.5 && bounced.state.motion.velocity[1] == 60 &&
              std::abs(bounced.state.motion.position[2] - depth.position[2]) < 1e-6 &&
              std::abs(bounced.state.motion.velocity[2] - depth.velocity[2]) < 1e-8,
          "Native XY response lost its bounce/tangent/depth semantics or subdivided a depth-only "
          "curve");
    for (unsigned sample = 0; sample <= 100; ++sample) {
        const auto seconds = .001 * sample;
        const auto observed = sample_particle_scene_motion(bounced, seconds);
        const auto exact = evaluate_particle_motion(falling.motion, force, 0, seconds);
        check(observed && std::abs(observed->position[2] - exact.position[2]) < 1e-6 &&
                  std::abs(observed->velocity[2] - exact.velocity[2]) < 1e-8,
              "XY contact interpolation replaced independent analytic depth with a chord");
    }
    ParticleSceneState overlapping{{{offset, offset, 9}, {0, 5, 0}}};
    const auto ended =
        advance_particle_scene_motion(overlapping, {}, 0, 0, settings, query, true, true);
    check(!ended.state.alive && ended.death == 0 && !sample_particle_scene_motion(ended, 0),
          "Native zero-normal XY overlap was fabricated into a contact normal or retained birth");
    ParticleSceneState turning{{{offset, offset + .2, 0}, {0, -40, 0}}};
    settings.bounce_limit = 0;
    const auto killed =
        advance_particle_scene_motion(turning, {0, 4000, 0}, 0, .02, settings, query, false, true);
    check(!killed.state.alive && killed.death && *killed.death > 0 && *killed.death < .01,
          "Curved XY contact missed its interior excursion");
    for (double radius : {.005, .05, 1., 500.}) {
        settings.radius = radius;
        settings.bounce_limit = 4;
        ParticleSceneState sized{{{offset, offset + radius + 1, 0}, {0, -120, 0}}};
        const auto step =
            advance_particle_scene_motion(sized, {}, 0, .02, settings, query, false, true);
        if (!step.state.alive || step.state.bounces != 1 ||
            step.state.motion.position[1] < offset + radius - 1e-5) {
            const auto contact = query(sized.motion.position, {0, -2.4, 0}, radius);
            std::cerr << Json{{"radius", radius},
                              {"offset", offset},
                              {"alive", step.state.alive},
                              {"bounces", step.state.bounces},
                              {"queries", step.queries},
                              {"position", step.state.motion.position},
                              {"velocity", step.state.motion.velocity}}
                             .dump()
                      << '\n';
            if (contact)
                std::cerr << Json{{"fraction", contact->fraction},
                                  {"point", contact->position},
                                  {"normal", contact->normal}}
                                 .dump()
                          << '\n';
        }
        check(step.state.alive && step.state.bounces == 1 &&
                  step.state.motion.position[1] >= offset + radius - 1e-5,
              "Admitted native circle size lost the contact at a supported world coordinate");
    }
}
void xy_compound_contacts() {
    using namespace physics_detail;
    for (bool sensor : {false, true}) {
        PhysicsConfig config;
        config.layers[1] = "Environment";
        Native2D native{config};
        Body2DConfiguration body;
        body.shape = 3;
        body.body.layer = 1;
        body.body.sensor = sensor;
        body.parts = std::make_shared<std::vector<BoxPart2D>>(
            std::initializer_list<BoxPart2D>{{{0, -.005f}, {20, .01f}}, {{0, 1.005f}, {20, .01f}}});
        const EntityRef ref{AssetId::generate(), EntityId::generate()};
        Desired2D desired{{1, {ref, body, {}}}};
        DesiredJoints2D joints;
        native.validate(desired, 1.f / 60, {}, {}, joints);
        std::vector<std::uint64_t> discontinuities;
        native.synchronize(desired, desired, 1.f / 60, {}, discontinuities, joints);
        native.finish_targets(1.f / 60);
        const auto query = [&](const Double3& origin, const Double3& displacement,
                               double radius) -> std::optional<PhysicsHit> {
            PhysicsSweep2D sweep;
            sweep.origin = {origin[0], origin[1]};
            sweep.displacement = {displacement[0], displacement[1]};
            sweep.dimensions = {radius, 0};
            const auto hit = native.shape_cast(sweep, {1u << 1, false});
            if (!hit)
                return {};
            return PhysicsHit{hit->entity,
                              {hit->position[0], hit->position[1], origin[2]},
                              {hit->normal[0], hit->normal[1], 0},
                              hit->fraction};
        };
        ParticleSceneCollision settings{.05, 1, 0, 4, 1u << 1};
        ParticleSceneState original{{{0, .5, 0}, {0, -20, 0}}};
        const auto result =
            advance_particle_scene_motion(original, {}, 0, .1, settings, query, false, true);
        check(result.state.alive &&
                  (sensor ? result.state.bounces == 0 && result.state.motion.position[1] == -1.5
                          : result.state.bounces == 2 && result.state.motion.position[1] > .05 &&
                                result.state.motion.position[1] < .95),
              "XY particle excluded another surface of one compound body or hit a sensor");
    }
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

void impact_records() {
    const EntityRef target{AssetId::generate(), EntityId::generate()};
    const ParticleSceneQuery wall = [&](const Double3& origin, const Double3& delta,
                                        double radius) -> std::optional<PhysicsHit> {
        const auto distance = .45 - radius - origin[0];
        if (delta[0] <= 0 || distance < 0 || distance > delta[0])
            return {};
        const auto fraction = distance / delta[0];
        return PhysicsHit{target,
                          {.45, origin[1] + delta[1] * fraction, origin[2] + delta[2] * fraction},
                          {-1, 0, 0},
                          fraction};
    };
    for (bool planar : {false, true}) {
        ParticleSceneCollision settings{.05, .5, .25, 4, UINT32_MAX};
        const ParticleSceneState initial{{{0, 0, 7}, {2, .5, 3}}};
        const auto old =
            advance_particle_scene_motion(initial, {}, 0, .4, settings, wall, false, planar);
        const auto captured =
            advance_particle_scene_motion(initial, {}, 0, .4, settings, wall, false, planar, true);
        check(old.impacts.empty() && captured.state == old.state &&
                  captured.queries == old.queries && captured.knots.size() == old.knots.size() &&
                  captured.impacts.size() == 1,
              "Impact collection changed legacy motion, work or trace membership");
        for (std::size_t i = 0; i < old.knots.size(); ++i)
            check(captured.knots[i].time == old.knots[i].time &&
                      captured.knots[i].motion == old.knots[i].motion,
                  "Impact collection changed legacy presentation knots");
        const auto& contact = captured.impacts.front();
        check(contact.collider == target && contact.kind == ParticleSceneImpactKind::Bounce &&
                  std::abs(contact.time - .2) < 1e-12 &&
                  std::abs(contact.position[0] - .4) < 1e-12 &&
                  contact.normal == Double3{-1, 0, 0} &&
                  contact.incoming_velocity == initial.motion.velocity &&
                  contact.outgoing_velocity == Double3{-1, .375, planar ? 3. : 2.25},
              "Impact record lost native identity, contact center, time or planar response");
        settings.bounce_limit = 0;
        const auto ended =
            advance_particle_scene_motion(initial, {}, 0, .4, settings, wall, false, planar, true);
        check(!ended.state.alive && ended.impacts.size() == 1 &&
                  ended.impacts.front().kind == ParticleSceneImpactKind::End &&
                  ended.impacts.front().time == *ended.death &&
                  ended.impacts.front().incoming_velocity ==
                      ended.impacts.front().outgoing_velocity,
              "Contact death omitted its terminal impact or invented a rebound");
        const ParticleSceneQuery invalid = [&](const auto& origin, const auto& delta,
                                               double radius) {
            auto hit = wall(origin, delta, radius);
            if (hit)
                hit->entity = {};
            return hit;
        };
        bool refused = false;
        try {
            (void)advance_particle_scene_motion(initial, {}, 0, .4, settings, invalid, false,
                                                planar, true);
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused, "Impact collection admitted an unbound collider identity");
    }
    const ParticleSceneState touching{{{0, 0, 7}, {2, .5, 3}}};
    const ParticleSceneQuery overlap = [&](const Double3& position, const auto&,
                                           double) -> std::optional<PhysicsHit> {
        return PhysicsHit{target, position, {}, 0};
    };
    const auto ended =
        advance_particle_scene_motion(touching, {}, 0, 0, {}, overlap, true, true, true);
    check(!ended.state.alive && ended.impacts.size() == 1 && ended.impacts.front().time == 0 &&
              ended.impacts.front().position == touching.motion.position &&
              ended.impacts.front().normal == Double3{} &&
              ended.impacts.front().kind == ParticleSceneImpactKind::End,
          "Defined XY initial overlap lost its zero-normal terminal observation");
    const ParticleSceneState departing{{{.4, 0, 7}, {-2, 0, 0}}};
    const ParticleSceneQuery touch = [&](const auto&, const auto&,
                                         double) -> std::optional<PhysicsHit> {
        return PhysicsHit{target, {.45, 0, 7}, {-1, 0, 0}, 0};
    };
    const auto touched =
        advance_particle_scene_motion(departing, {}, 0, 0, {}, touch, true, false, true);
    check(touched.state.alive && touched.state.bounces == 0 && touched.impacts.size() == 1 &&
              touched.impacts.front().kind == ParticleSceneImpactKind::Touch &&
              touched.impacts.front().incoming_velocity ==
                  touched.impacts.front().outgoing_velocity,
          "A departing contact was misreported as a rebound or death");
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
        impact_records();
        xy_compound_contacts();
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
        for (double offset : {0., 900000., -900000.})
            xy_cases(offset);
        std::cout << "Scene particle native motion and settings passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
