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
void stationary_rest(const ParticleSceneQuery& query, Double3 position, bool planar) {
    const ParticleSceneCollision settings{.05, 0, 1, 4, UINT32_MAX};
    const ParticleSceneState initial{{position, {0, -1, planar ? 2. : 0.}}};
    const Double3 acceleration{0, -4, planar ? -3. : 0.};
    const auto old = advance_particle_scene_motion(initial, acceleration, 0, .3, settings, query,
                                                   false, planar, true);
    check(!old.state.alive && old.death && !old.state.support,
          "The legacy zero-restitution profile gained resting state");
    const auto held = advance_particle_scene_motion(initial, acceleration, 0, .3, settings, query,
                                                    false, planar, true, true);
    check(held.state.alive && held.state.support && !held.death && held.state.bounces == 1 &&
              held.state.motion.velocity[1] == 0 && held.impacts.size() == 1 &&
              held.impacts.front().kind == ParticleSceneImpactKind::Bounce,
          "Opted-in native particle did not settle without duplicate contact outcomes");
    const auto sampled = sample_particle_scene_motion(held, .25);
    check(sampled && std::abs(sampled->position[1] - held.state.motion.position[1]) < 1e-8,
          "Supported presentation fell through its plane between fixed endpoints");
    if (planar)
        check(std::abs(held.state.motion.position[2] - (position[2] + .6 - .135)) < 1e-8 &&
                  std::abs(sampled->position[2] - (position[2] + .5 - .09375)) < 1e-8,
              "XY resting constrained analytic depth");
    auto state = held.state;
    for (unsigned i = 0; i < 4; ++i) {
        const auto next = advance_particle_scene_motion(state, acceleration, 0, .1, settings, query,
                                                        false, planar, true, true);
        check(next.state.alive && next.state.support && next.queries == 1 &&
                  next.state.bounces == held.state.bounces && next.impacts.size() == 1 &&
                  next.impacts.front().kind == ParticleSceneImpactKind::Touch &&
                  std::abs(next.state.motion.position[1] - held.state.motion.position[1]) < 1e-8,
              "Stationary support bounced, drifted, lost native identity or spent excess queries");
        state = next.state;
    }
    auto sliding = held.state;
    sliding.motion.velocity[0] = .25;
    const auto moved = advance_particle_scene_motion(sliding, acceleration, 0, .1, settings, query,
                                                     false, planar, true, true);
    check(moved.state.alive && moved.state.support && moved.state.bounces == held.state.bounces &&
              moved.state.motion.velocity[0] == .25 &&
              std::abs(moved.state.motion.position[0] - (sliding.motion.position[0] + .025)) <
                  5e-7 &&
              std::abs(moved.state.motion.position[1] - held.state.motion.position[1]) < 1e-8 &&
              moved.impacts.size() == 1 && moved.queries <= 40,
          "Supported native slide lost tangent velocity, contact plane, notification or budget");
    const auto gone = [](const Double3&, const Double3&, double) -> std::optional<PhysicsHit> {
        return {};
    };
    const auto released = advance_particle_scene_motion(state, acceleration, 0, .1, settings, gone,
                                                        false, planar, true, true);
    check(released.state.alive && !released.state.support && released.impacts.empty() &&
              released.state.motion.position[1] < state.motion.position[1] - .019 &&
              state.support.has_value(),
          "Missing support did not release gravity or mutated the prior candidate");
}
void native_support_edge(const ParticleSceneQuery& query, double offset, bool planar) {
    const ParticleSceneState initial{{{offset + 9.9, .15, 0}, {1, -1, 0}}};
    const auto result = advance_particle_scene_motion(
        initial, {0, -4, 0}, 0, .5, ParticleSceneCollision{.05, 0, 0, 16, UINT32_MAX}, query, false,
        planar, true, true);
    check(result.state.alive && !result.state.support && result.state.motion.position[1] < -.02 &&
              result.state.motion.position[0] > offset + 10.2 &&
              result.queries <= particle_scene_birth_query_limit && initial.bounces == 0 &&
              !initial.support,
          "Native finite support did not release a sliding birth beyond its edge");
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
    stationary_rest(query, {offset, offset + .15, 7}, true);
    if (offset == 0)
        native_support_edge(query, offset, true);
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
    stationary_rest(query, {offset, .15, 7}, false);
    if (offset == 0)
        native_support_edge(query, offset, false);
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
void resting_mock() {
    const EntityRef target{AssetId::generate(), EntityId::generate()};
    const ParticleSceneQuery floor = [&](const Double3& origin, const Double3& delta,
                                         double radius) -> std::optional<PhysicsHit> {
        if (delta[1] >= 0)
            return {};
        const auto fraction = std::max(0., (radius - origin[1]) / delta[1]);
        if (fraction > 1)
            return {};
        return PhysicsHit{target,
                          {origin[0] + delta[0] * fraction, 0, origin[2] + delta[2] * fraction},
                          {0, 1, 0},
                          fraction};
    };
    stationary_rest(floor, {0, .15, 7}, false);
    stationary_rest(floor, {0, .15, 7}, true);
    for (bool planar : {false, true}) {
        const auto edge = [&](const Double3& origin, const Double3& delta,
                              double radius) -> std::optional<PhysicsHit> {
            auto hit = floor(origin, delta, radius);
            return hit && hit->position[0] <= .1 ? hit : std::optional<PhysicsHit>{};
        };
        const auto departing = advance_particle_scene_motion(
            {{{0, .15, 0}, {1, -1, 0}}}, {0, -4, 0}, 0, .3,
            ParticleSceneCollision{.05, 0, 0, 4, UINT32_MAX}, edge, false, planar, true, true);
        check(departing.state.alive && !departing.state.support &&
                  std::abs(departing.state.motion.position[0] - .3) < 1e-10 &&
                  departing.state.motion.position[1] < .02 && departing.queries < 120,
              "Sliding across a support edge did not release within its bounded interval");
    }
    ParticleSceneCollision kill{.05, 0, 1, 0, UINT32_MAX};
    const auto ended = advance_particle_scene_motion({{{0, .15, 0}, {0, -1, 0}}}, {0, -4, 0}, 0, .3,
                                                     kill, floor, false, false, true, true);
    check(!ended.state.alive && !ended.state.support && ended.impacts.size() == 1 &&
              ended.impacts.front().kind == ParticleSceneImpactKind::End,
          "Resting overrode explicit zero permitted rebounds");
    const ParticleSceneCollision settings{.05, 0, 0, 4, UINT32_MAX};
    const ParticleSceneState supported{
        {{0, .050005, 0}, {0, 0, 0}}, 1, true, ParticleSceneSupport{target, {0, 1, 0}}};
    const auto fails = [&](const ParticleSceneState& state, const ParticleSceneQuery& query,
                           bool planar = false) {
        const auto before = state;
        bool rejected = false;
        try {
            advance_particle_scene_motion(state, {0, -4, 0}, 0, .1, settings, query, false, planar,
                                          true, true);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && state == before,
              "Invalid resting state/query partially adopted a motion candidate");
    };
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto state = supported;
        if (invalid == 0)
            state.support->collider = {};
        else if (invalid == 1)
            state.support->normal = {};
        else if (invalid == 2)
            state.support->normal = {0, 2, 0};
        else
            state.support->normal = {0, 0, 1};
        fails(state, floor, invalid == 3);
    }
    for (unsigned invalid = 0; invalid < 4; ++invalid)
        fails(supported,
              [&](const Double3& origin, const Double3& delta,
                  double radius) -> std::optional<PhysicsHit> {
                  auto hit = *floor(origin, delta, radius);
                  if (invalid == 0)
                      throw std::runtime_error("Native support query failure");
                  if (invalid == 1)
                      hit.fraction = 2;
                  else if (invalid == 2)
                      hit.position[0] = std::numeric_limits<double>::infinity();
                  else
                      hit.normal[0] = std::numeric_limits<double>::quiet_NaN();
                  return hit;
              });
    auto sliding = supported;
    sliding.motion.velocity[0] = 100;
    unsigned calls = 0;
    fails(sliding, [&](const Double3& origin, const Double3& delta, double radius) {
        ++calls;
        return floor(origin, delta, radius);
    });
    check(calls == particle_scene_birth_query_limit,
          "Support probes bypassed the existing per-birth native-query ceiling");
    const EntityRef replacement{target.scene, EntityId::generate()};
    const auto replaced = [&](const Double3& origin, const Double3& delta,
                              double radius) -> std::optional<PhysicsHit> {
        auto hit = floor(origin, delta, radius);
        if (hit)
            hit->entity = replacement;
        return hit;
    };
    const auto renewed = advance_particle_scene_motion(supported, {0, -4, 0}, 0, .1, settings,
                                                       replaced, false, false, true, true);
    check(renewed.state.alive && renewed.state.support &&
              renewed.state.support->collider == replacement && renewed.state.bounces == 2,
          "A replacement surface inherited the retired support identity without a new contact");
    const Double3 normal{.6, .8, 0};
    const auto slope = [&](const Double3& origin, const Double3& delta,
                           double radius) -> std::optional<PhysicsHit> {
        const auto dot = [&](const Double3& v) { return v[0] * normal[0] + v[1] * normal[1]; };
        const auto along = dot(delta);
        if (along >= 0)
            return {};
        const auto fraction = (radius - dot(origin)) / along;
        if (fraction < 0 || fraction > 1)
            return {};
        Double3 point;
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] = origin[axis] + delta[axis] * fraction - radius * normal[axis];
        return PhysicsHit{target, point, normal, fraction};
    };
    for (bool planar : {false, true}) {
        ParticleSceneState tilted{{{.050005 * .6, .050005 * .8, 7}, {0, 0, 0}},
                                  1,
                                  true,
                                  ParticleSceneSupport{target, normal}};
        const auto held = advance_particle_scene_motion(tilted, {-2.4, -3.2, 0}, 2, .1, settings,
                                                        slope, false, planar, true, true);
        check(held.state.alive && held.state.support && held.queries == 1 &&
                  held.state.bounces == 1 && held.state.motion == tilted.motion,
              "A sloped support changed identity, drifted or invented extra bounces");
    }
}
void moving_surface_coordinates() {
    for (bool planar : {false, true})
        for (double offset : {0., 900000., -900000.})
            for (double direction : {-1., 1.}) {
                EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
                Scene scene(engine.world());
                auto physics =
                    std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
                auto wall = box("wall", offset - direction, 0, 2, 0, false);
                auto& components = wall["components"];
                if (planar) {
                    components.erase("forge.physics_body");
                    components.erase("forge.box_collider");
                    components["forge.physics_body_2d"] = {{"motion", 1}, {"gravity_factor", 0}};
                    components["forge.box_collider_2d"] = {{"x", .05}, {"y", 2}};
                } else {
                    components["forge.physics_body"]["motion"] = 1;
                    components["forge.box_collider"] = {{"x", .05}, {"y", 2}, {"z", 2}};
                }
                scene.restore_snapshot({{"version", 1}, {"entities", Json::array({wall})}});
                physics->synchronize(0);
                physics->enable_motion_queries();
                scene.entity("wall").set<LocalTranslation>({offset + direction, 0, 0});
                physics->synchronize(.1f);
                physics->step(.1f);
                physics->adopt();
                const auto expected = *engine.world().reference(scene.entity("wall").id());
                const auto inspect = [&](const auto& result) {
                    check(result.hit && result.queries == (planar ? 2u : 1u) &&
                              result.hit->contact.entity == expected &&
                              std::abs(result.hit->contact.fraction - .4625) < 1e-5 &&
                              std::abs(result.hit->contact.position[0] -
                                       (offset - direction * .05)) < 2e-5 &&
                              result.hit->contact.normal[0] == direction &&
                              std::abs(result.hit->surface_velocity[0] - direction * 20) < 1e-5,
                          "Native moving-surface query lost direction, large-origin precision or "
                          "provenance");
                };
                if (planar) {
                    PhysicsSweep2D sweep;
                    sweep.origin = {offset, 0};
                    sweep.dimensions = {.05, 0};
                    inspect(physics->shape_cast_motion_2d(sweep, {}, {}));
                } else {
                    PhysicsSweep sweep;
                    sweep.origin = {offset, 0, 0};
                    sweep.dimensions = {.05, 0, 0};
                    inspect(physics->shape_cast_motion(sweep, {}, {}));
                }
            }
}
void moving_surface_baseline() {
    for (bool planar : {false, true}) {
        EngineContext engine{WorldRole::Runtime, false, {physics_module()}};
        Scene scene(engine.world());
        auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
        auto wall = box("moving-wall", -1, 0, 2, 0, false);
        auto& components = wall["components"];
        if (planar) {
            components.erase("forge.physics_body");
            components.erase("forge.box_collider");
            components["forge.physics_body_2d"] = {{"motion", 1}, {"gravity_factor", 0}};
            components["forge.box_collider_2d"] = {{"x", .05}, {"y", 2}};
        } else {
            components["forge.physics_body"]["motion"] = 1;
            components["forge.box_collider"] = {{"x", .05}, {"y", 2}, {"z", 2}};
        }
        scene.restore_snapshot({{"version", 1}, {"entities", Json::array({wall})}});
        physics->synchronize(0);
        physics->enable_motion_queries();
        const ParticleSceneQuery query = [&](const Double3& origin, const Double3& delta,
                                             double radius) -> std::optional<PhysicsHit> {
            if (planar) {
                PhysicsSweep2D sweep;
                sweep.origin = {origin[0], origin[1]};
                sweep.displacement = {delta[0], delta[1]};
                sweep.dimensions = {radius, 0};
                const auto hit = physics->shape_cast_2d(sweep, {});
                if (!hit)
                    return {};
                return PhysicsHit{hit->entity,
                                  {hit->position[0], hit->position[1], origin[2]},
                                  {hit->normal[0], hit->normal[1], 0},
                                  hit->fraction};
            }
            PhysicsSweep sweep;
            sweep.origin = {origin[0], origin[1], origin[2]};
            sweep.displacement = delta;
            sweep.dimensions = {radius, 0, 0};
            return physics->shape_cast(sweep, {});
        };
        check(query({-1, 0, 0}, {}, .05).has_value() && !query({}, {}, .05),
              "Moving-wall baseline did not realize the initial native geometry");
        scene.entity("moving-wall").set<LocalTranslation>({1, 0, 0});
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        check(query({1, 0, 0}, {}, .05).has_value() && !query({-1, 0, 0}, {}, .05),
              "Moving-wall baseline did not adopt the kinematic target");
        const ParticleSceneState initial{{{0, 0, 0}, {0, 0, 0}}};
        const auto legacy =
            advance_particle_scene_motion(initial, {}, 0, .1, {}, query, false, planar, true);
        check(legacy.state == initial && legacy.queries == 1 && legacy.impacts.empty(),
              "Legacy current-snapshot response unexpectedly inferred historical wall motion");
        const auto frame = physics->motion_frame();
        check(frame.ready && frame.tick == 1 && std::abs(frame.dt - .1) < 1e-8,
              "Motion query lost adopted interval provenance");
        const auto motion = [&](PhysicsQuerySlice interval, PhysicsQueryFilter filter = {},
                                const ParticleSceneSweep* request =
                                    nullptr) -> PhysicsMotionResult {
            if (planar) {
                PhysicsSweep2D sweep;
                sweep.dimensions = {.05, 0};
                if (request) {
                    sweep.origin = {request->origin[0], request->origin[1]};
                    sweep.displacement = {request->displacement[0], request->displacement[1]};
                    sweep.dimensions[0] = request->radius;
                }
                const auto observed = physics->shape_cast_motion_2d(sweep, interval, filter);
                PhysicsMotionResult result{{}, observed.queries};
                if (observed.hit) {
                    const auto& hit = *observed.hit;
                    result.hit =
                        PhysicsMotionHit{{hit.contact.entity,
                                          {hit.contact.position[0], hit.contact.position[1],
                                           request ? request->origin[2] + request->displacement[2] *
                                                                              hit.contact.fraction
                                                   : 0},
                                          {hit.contact.normal[0], hit.contact.normal[1], 0},
                                          hit.contact.fraction},
                                         {hit.surface_velocity[0], hit.surface_velocity[1], 0}};
                }
                return result;
            }
            PhysicsSweep sweep;
            sweep.dimensions = {.05, 0, 0};
            if (request) {
                sweep.origin = {request->origin[0], request->origin[1], request->origin[2]};
                sweep.displacement = request->displacement;
                sweep.dimensions[0] = request->radius;
            }
            return physics->shape_cast_motion(sweep, interval, filter);
        };
        const double tolerance = planar ? .006 : 1e-4;
        const auto crossed = motion({});
        const unsigned crossing_work = planar ? 2u : 1u;
        check(crossed.hit && crossed.queries == crossing_work &&
                  std::abs(crossed.hit->contact.fraction - .4625) < tolerance &&
                  std::abs(crossed.hit->contact.position[0] + .05) < tolerance &&
                  crossed.hit->contact.normal == Double3{1, 0, 0} &&
                  std::abs(crossed.hit->surface_velocity[0] - 20) < 1e-5,
              "Adopted native motion query missed a stationary birth or changed its witness");
        const auto slice = motion({.25, .75, crossing_work});
        check(slice.hit && slice.queries == crossing_work &&
                  std::abs(slice.hit->contact.fraction - .425) < tolerance &&
                  std::abs(slice.hit->contact.position[0] + .05) < tolerance &&
                  slice.hit->surface_velocity == crossed.hit->surface_velocity,
              "Motion interval slice changed physical-time witness or surface velocity");
        const auto late = motion({.6, 1, 1});
        check(!late.hit && late.queries == 1,
              "A later query slice replayed a contact from the earlier interval");
        const auto endpoint = motion({0, crossed.hit->contact.fraction, crossing_work});
        if (!endpoint.hit || std::abs(endpoint.hit->contact.fraction - 1) >= 1e-5)
            std::cerr << Json{{"planar", planar},
                              {"endpoint_hit", bool(endpoint.hit)},
                              {"queries", endpoint.queries},
                              {"source_fraction", crossed.hit->contact.fraction},
                              {"fraction", endpoint.hit ? endpoint.hit->contact.fraction : -1}}
                             .dump()
                      << '\n';
        check(endpoint.hit && endpoint.queries == crossing_work &&
                  std::abs(endpoint.hit->contact.fraction - 1) < 1e-5 &&
                  std::abs(endpoint.hit->contact.position[0] + .05) < 2e-5,
              "Native moving-surface query lost first contact at its interval endpoint");
        const auto rejected = [&](PhysicsQuerySlice interval, PhysicsQueryFilter filter = {}) {
            const auto before = physics->motion_frame();
            bool failed = false;
            try {
                (void)motion(interval, filter);
            } catch (const std::exception&) {
                failed = true;
            }
            const auto after = physics->motion_frame();
            check(failed && before.tick == after.tick && before.dt == after.dt &&
                      before.ready == after.ready,
                  "Rejected motion query changed its accepted native interval");
        };
        for (const PhysicsQuerySlice interval :
             {PhysicsQuerySlice{-.1, 1, 1}, PhysicsQuerySlice{0, 1.1, 1},
              PhysicsQuerySlice{.8, .2, 1}, PhysicsQuerySlice{0, 1, 0},
              PhysicsQuerySlice{0, 1, physics_motion_query_limit + 1},
              PhysicsQuerySlice{std::numeric_limits<double>::quiet_NaN(), 1, 1},
              PhysicsQuerySlice{0, std::numeric_limits<double>::infinity(), 1}})
            rejected(interval);
        const auto repeated = motion({});
        check(repeated.hit && repeated.hit->contact.entity == crossed.hit->contact.entity &&
                  repeated.hit->contact.fraction == crossed.hit->contact.fraction &&
                  repeated.hit->contact.position == crossed.hit->contact.position,
              "Repeated native motion reads consumed or changed observations");
        const auto masked = motion({}, PhysicsQueryFilter{0, false});
        const std::array excluded{crossed.hit->contact.entity};
        const auto ignored = motion({}, PhysicsQueryFilter{UINT32_MAX, false, excluded});
        check(!masked.hit && !ignored.hit && masked.queries == 1 && ignored.queries == 1,
              "Motion queries bypassed masks/exclusions or provided a zero-cost empty call");
        const ParticleSceneMotionQuery moving_query = [&](const ParticleSceneSweep& request) {
            const auto observed = motion({request.start_seconds / frame.dt,
                                          request.end_seconds / frame.dt, request.query_budget},
                                         PhysicsQueryFilter{UINT32_MAX, false}, &request);
            ParticleSceneQueryResult result{{}, observed.queries};
            if (observed.hit)
                result.hit =
                    ParticleSceneHit{observed.hit->contact, observed.hit->surface_velocity};
            return result;
        };
        const auto reacted = advance_particle_scene_motion(
            initial, {}, 0, frame.dt, ParticleSceneCollision{.05, .5, 0, 4, UINT32_MAX}, query,
            false, planar, true, false, moving_query);
        check(reacted.state.alive && reacted.state.bounces == 1 && reacted.impacts.size() == 1 &&
                  reacted.impacts.front().kind == ParticleSceneImpactKind::Bounce &&
                  reacted.impacts.front().incoming_velocity[0] == 0 &&
                  std::abs(reacted.impacts.front().outgoing_velocity[0] - 30) < 1e-5 &&
                  std::abs(reacted.impacts.front().time - frame.dt * .4625) < 1e-5 &&
                  std::abs(reacted.state.motion.position[0] - (planar ? 1.6175 : 1.612505)) <
                      1e-4 &&
                  initial.motion.position[0] == 0,
              "Native moving wall lost relative response, corrected contact time or atomic input");
        const ParticleSceneCollision resting{.05, 0, 0, 4, UINT32_MAX};
        const ParticleSceneMotionQuery scaled_query = [&](const ParticleSceneSweep& request) {
            auto mapped = request;
            mapped.start_seconds = frame.dt * .45 + request.start_seconds * .05;
            mapped.end_seconds = frame.dt * .45 + request.end_seconds * .05;
            auto result = moving_query(mapped);
            if (result.hit)
                for (auto& velocity : result.hit->surface_velocity)
                    velocity *= .05;
            return result;
        };
        const auto scaled = advance_particle_scene_motion(
            initial, {-4, 0, 0}, 0, .2, ParticleSceneCollision{.05, 0, .25, 4, UINT32_MAX}, query,
            false, planar, true, true, scaled_query);
        if (!scaled.state.alive) {
            Json outcomes = Json::array();
            for (const auto& hit : scaled.impacts)
                outcomes.push_back({{"kind", unsigned(hit.kind)},
                                    {"time", hit.time},
                                    {"normal", hit.normal},
                                    {"position", hit.position},
                                    {"incoming", hit.incoming_velocity},
                                    {"outgoing", hit.outgoing_velocity}});
            throw std::runtime_error(std::string(planar ? "XY" : "3D") +
                                     " scaled native support ended: " + outcomes.dump());
        }
        const auto held =
            advance_particle_scene_motion(initial, {-4, 0, 0}, 0, frame.dt * .5, resting, query,
                                          false, planar, true, true, moving_query);
        check(held.state.alive && held.state.support && held.state.bounces == 1 &&
                  std::abs(held.state.motion.velocity[0] - 20) < 1e-5 &&
                  std::abs(held.state.motion.position[0] - (planar ? .08 : .075005)) < 2e-5 &&
                  held.impacts.size() == 1 && held.queries <= particle_scene_birth_query_limit,
              "Native moving support failed to retain relative normal response or bounded work");
        const ParticleSceneMotionQuery supported_query = [&](const ParticleSceneSweep& request) {
            auto mapped = request;
            mapped.start_seconds += frame.dt * .5;
            mapped.end_seconds += frame.dt * .5;
            return moving_query(mapped);
        };
        const auto carried =
            advance_particle_scene_motion(held.state, {-4, 0, 0}, 2, .002, resting, query, false,
                                          planar, true, true, supported_query);
        check(carried.state.alive && carried.state.support && carried.state.bounces == 1 &&
                  std::abs(carried.state.motion.position[0] - held.state.motion.position[0] - .04) <
                      2e-5 &&
                  std::abs(carried.state.motion.velocity[0] - 20) < 1e-5 &&
                  carried.impacts.size() == 1 &&
                  carried.impacts.front().kind == ParticleSceneImpactKind::Touch,
              "Moving native support lost constrained drag, velocity or continuing Touch outcome");
        physics->synchronize(0);
        check(!physics->motion_frame().ready,
              "New native synchronization retained an adopted motion interval");
        rejected({});
        auto first = wall, second = wall;
        first["id"] = first["name"] = "near-a";
        second["id"] = second["name"] = "near-b";
        first["components"]["forge.position"]["x"] = .05;
        second["components"]["forge.position"]["x"] = .05;
        first["components"][planar ? "forge.physics_body_2d" : "forge.physics_body"]["sensor"] =
            true;
        scene.restore_snapshot({{"version", 1}, {"entities", Json::array({first, second})}});
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        const auto sensor = *engine.world().reference(scene.entity("near-a").id());
        const auto solid = *engine.world().reference(scene.entity("near-b").id());
        const auto all = motion({});
        const auto expected =
            std::tie(sensor.scene, sensor.entity) < std::tie(solid.scene, solid.entity) ? sensor
                                                                                        : solid;
        if (!all.hit || all.queries != 2 || all.hit->contact.fraction != 0 ||
            all.hit->contact.entity != expected)
            std::cerr << Json{{"planar", planar},
                              {"queries", all.queries},
                              {"hit", bool(all.hit)},
                              {"fraction", all.hit ? all.hit->contact.fraction : -1},
                              {"actual", all.hit ? all.hit->contact.entity.entity.str() : ""},
                              {"expected", expected.entity.str()}}
                             .dump()
                      << '\n';
        check(all.hit && all.queries == 2 && all.hit->contact.fraction == 0 &&
                  all.hit->contact.entity == expected,
              "Equal-time motion hits lost complete native work or canonical selection");
        const auto solids = motion({0, 1, 1}, PhysicsQueryFilter{UINT32_MAX, false});
        check(solids.hit && solids.queries == 1 && solids.hit->contact.entity == solid,
              "Ignored sensors spent narrow-phase work or hid the solid motion hit");
        rejected({0, 1, 1});
        // Discontinuous placement is admitted before the observed step. It must
        // never become a historical sweep through intervening particles.
        const auto lifecycle_query = [&](const char* phase) {
            try {
                return motion({});
            } catch (const std::exception& error) {
                const auto entity = scene.entity("moving-wall");
                std::string velocity;
                if (!planar && entity.is_alive())
                    if (const auto state =
                            physics->body_state(*engine.world().reference(entity.id())))
                        velocity = " angular_velocity=" + Json(state->angular_velocity).dump();
                if (planar && entity.is_alive())
                    if (const auto state =
                            physics->body_state_2d(*engine.world().reference(entity.id())))
                        velocity = " angular_velocity=" + Json(state->angular_velocity).dump();
                throw std::runtime_error(std::string(planar ? "XY " : "3D ") + phase + ": " +
                                         error.what() + velocity);
            }
        };
        scene.restore_snapshot({{"version", 1}, {"entities", Json::array({wall})}});
        physics->synchronize(0);
        const auto wall_ref = *engine.world().reference(scene.entity("moving-wall").id());
        if (planar)
            physics->teleport_2d(wall_ref, {1, 0}, 0, true);
        else
            physics->teleport(wall_ref, {1, 0, 0}, {}, true);
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        check(!lifecycle_query("teleport").hit && query({1, 0, 0}, {}, .05),
              "Teleport invented a historical moving-surface sweep");
        scene.entity("moving-wall").destruct();
        physics->synchronize(.1f);
        check(!physics->motion_frame().ready,
              "Native retirement left the prior motion interval available");
        physics->step(.1f);
        physics->adopt();
        check(!motion({}).hit && motion({}).queries == 1,
              "Retired native geometry remained in the motion query");
        auto rotating = wall;
        rotating["components"]["forge.position"]["x"] = 0;
        scene.restore_snapshot({{"version", 1}, {"entities", Json::array({rotating})}});
        physics->synchronize(0);
        scene.entity("moving-wall")
            .set<LocalRotation>({0, 0, float(std::sin(.1)), float(std::cos(.1))});
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        rejected({});
        const auto rotating_ref = *engine.world().reference(scene.entity("moving-wall").id());
        const std::array ignore_rotation{rotating_ref};
        check(!motion({}, PhysicsQueryFilter{0, false}).hit &&
                  !motion({}, PhysicsQueryFilter{UINT32_MAX, false, ignore_rotation}).hit,
              "Filtered rotating geometry rejected an otherwise empty motion query");
        // A fixed rotated pose is still a supported linear target. Only actual
        // angular activity during the observed interval is unsupported.
        scene.entity("moving-wall").destruct();
        physics->synchronize(0);
        rotating["components"]["forge.rotation"] = {
            {"x", planar ? 0 : 17}, {"y", planar ? 0 : 23}, {"z", 25}};
        scene.restore_snapshot({{"version", 1}, {"entities", Json::array({rotating})}});
        physics->synchronize(0);
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        check(lifecycle_query("fixed rotated pose").hit.has_value(),
              "Unchanged rotated geometry was rejected");
        const auto far_ref = *engine.world().reference(scene.entity("moving-wall").id());
        if (planar)
            physics->teleport_2d(far_ref, {1000, 0}, 0, true);
        else
            physics->teleport(far_ref, {1000, 0, 0}, {}, true);
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        scene.entity("moving-wall")
            .set<LocalRotation>({0, 0, float(std::sin(.2)), float(std::cos(.2))});
        physics->synchronize(.1f);
        physics->step(.1f);
        physics->adopt();
        check(!lifecycle_query("distant rotation").hit,
              "Distant rotating geometry rejected an unrelated motion query");
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
        impact_records();
        resting_mock();
        moving_surface_baseline();
        moving_surface_coordinates();
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
