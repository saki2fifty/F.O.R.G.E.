#include "particle_motion.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void close(double actual, double expected, const char* why, double tolerance = 1e-10) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, why);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid particle plane motion was admitted");
}
ParticleCollisionPlane floor(unsigned bounces = 4, double restitution = 1, double friction = 0) {
    return prepare_particle_collision_plane({{0, 1, 0}, 0, 0, restitution, friction, bounces});
}
void exact_contacts() {
    const ParticleMotionState initial{{0, 1, 0}, {2, 0, 0}};
    const std::array acceleration{0., -2., 0.};
    for (double age : {0., .5, std::nextafter(1., 0.), 1., 1.5, 2., 2.5, 3., 5., 9.}) {
        const auto result = evaluate_particle_plane_motion(initial, acceleration, 0, age, floor());
        close(result.state.position[0], age * 2, "Frictionless collision changed tangent motion");
        if (age < 9) {
            check(!result.death_age, "Elastic particle died before its allowed contacts");
            const auto phase = age < 1 ? age : std::fmod(age - 1, 2.);
            close(result.state.position[1], age < 1 ? 1 - age * age : 2 * phase - phase * phase,
                  "Elastic gravity bounce differs from its closed-form parabola");
            close(result.state.velocity[1], age < 1 ? -2 * age : 2 - 2 * phase,
                  "Exact incoming contact did not publish the outgoing velocity");
        } else {
            check(result.bounces == 4 && result.death_age,
                  "Bounce limit did not terminate on the next incoming contact");
            close(*result.death_age, 9, "Wrong exhaustion contact age");
        }
    }
    const auto reduced =
        evaluate_particle_plane_motion(initial, acceleration, 0, 2.25, floor(2, .5, .5));
    check(!reduced.death_age && reduced.bounces == 2, "Restitution lost a completed bounce");
    close(reduced.state.position[0], 3.125,
          "Friction did not multiply tangent velocity per contact");
    close(reduced.state.velocity[0], .5, "Friction changed the wrong velocity channel");
    close(reduced.state.position[1], .0625, "Restitution did not scale normal rebound speed");
    const auto dead = evaluate_particle_plane_motion(initial, acceleration, 0, 3, floor(2, .5, .5));
    check(dead.death_age && dead.bounces == 2, "Exhausted rebound count remained alive");
    close(*dead.death_age, 2.5, "Reduced restitution death age drifted");
    const auto killed = evaluate_particle_plane_motion(initial, acceleration, 0, 2, floor(0));
    check(killed.death_age && !killed.bounces, "Kill-on-first-contact performed a bounce");
    close(*killed.death_age, 1, "Kill plane tunneled through the first impact");
}
void plane_geometry() {
    auto plane = floor();
    plane.normal = {0, 8, 0};
    plane.offset = 2;
    plane.radius = .5;
    plane = prepare_particle_collision_plane(plane);
    const auto shifted = evaluate_particle_plane_motion({{0, 3.5, 0}, {0, -2, 0}}, {}, 0, 1, plane);
    check(shifted.bounces == 1 && !shifted.death_age, "Offset/radius plane lost the collision");
    close(shifted.state.position[1], 3.5, "Plane offset/radius used unnormalized distance units");
    close(shifted.state.velocity[1], 2, "Scaled normal changed restitution");
    const auto q = std::sqrt(.5);
    auto oblique = floor();
    oblique.normal = {2, 2, 0};
    oblique = prepare_particle_collision_plane(oblique);
    const auto diagonal =
        evaluate_particle_plane_motion({{q, q, 0}, {-2 * q, -2 * q, 3}}, {}, 0, 1, oblique);
    check(diagonal.bounces == 1 && !diagonal.death_age, "Oblique plane missed a swept point");
    close(diagonal.state.position[0], q, "Oblique response changed the collision plane");
    close(diagonal.state.position[1], q, "Oblique response changed the normal distance");
    close(diagonal.state.position[2], 3, "Oblique response changed tangent displacement");
    const auto fast =
        evaluate_particle_plane_motion({{0, .001, 0}, {0, -1000, 0}}, {}, 0, 1, floor(0));
    check(bool(fast.death_age), "High-speed particle crossed the plane between samples");
    close(*fast.death_age, .000001, "High-speed contact used a frame endpoint", 1e-16);
}
void grazing_and_drag() {
    const auto plane = floor();
    const ParticleMotionState touching{{}, {2, 0, 0}};
    const auto tangent = evaluate_particle_plane_motion(touching, {}, 0, 2, plane);
    check(!tangent.death_age && !tangent.bounces && tangent.state.position[0] == 4,
          "Tangential contact consumed a collision or invented friction");
    const auto resting = evaluate_particle_plane_motion(touching, {0, -2, 0}, 0, 0, plane);
    check(resting.death_age && *resting.death_age == 0,
          "Sustained inward contact entered an unbounded zero-time bounce loop");
    const auto sliding =
        evaluate_particle_plane_motion({{0, 1, 0}, {2, -2, 0}}, {}, 0, 2, floor(1, 0, .5));
    check(!sliding.death_age && sliding.bounces == 1,
          "Zero normal force killed a particle sliding along the plane");
    close(sliding.state.position[0], 2.5, "Zero-rebound sliding lost tangent friction");
    close(sliding.state.position[1], 0, "Zero-rebound sliding left the plane");
    const auto below = evaluate_particle_plane_motion({{0, -.1, 0}, {0, 2, 0}}, {}, 0, 1, plane);
    check(below.death_age && *below.death_age == 0, "Initially penetrating birth was teleported");
    const auto outgoing =
        evaluate_particle_plane_motion({{}, {0, 2, 0}}, {0, -2, 0}, 0, 2, floor(0));
    check(bool(outgoing.death_age), "Outgoing boundary particle never returned to the plane");
    close(*outgoing.death_age, 2, "Outgoing boundary particle collided with its birth contact");
    const auto tangent_arc =
        evaluate_particle_plane_motion({{0, 1, 0}, {0, -2, 0}}, {0, 2, 0}, 0, 3, plane);
    check(!tangent_arc.death_age && !tangent_arc.bounces,
          "A nonpenetrating tangent arc was treated as an incoming crossing");
    close(tangent_arc.state.position[1], 4, "Tangent arc changed its analytic path");
    const auto hidden =
        evaluate_particle_plane_motion({{0, 1, 0}, {0, -3, 0}}, {0, 2, 0}, 0, 3, floor(0));
    check(bool(hidden.death_age), "Two plane crossings between query endpoints were missed");
    close(*hidden.death_age, (3 - std::sqrt(5.)) / 2, "First incoming root was not selected");
    for (double drag : {1e-12, .25, 2., 64.}) {
        const auto age = .75;
        const auto result =
            evaluate_particle_plane_motion({{0, .5, 0}, {1, -2, 0}}, {}, drag, age, floor(0));
        if (drag < 4) {
            check(bool(result.death_age), "Finite damped crossing remained alive");
            close(*result.death_age, -std::log1p(-.25 * drag) / drag,
                  "Damped contact differs from the independent logarithmic root");
        } else
            check(!result.death_age && !result.bounces, "Drag-limited excursion crossed the plane");
    }
    for (double age : {1., 10., 30.}) {
        const auto asymptote =
            evaluate_particle_plane_motion({{0, 1, 0}, {0, -2, 0}}, {}, 2, age, floor(0));
        check(!asymptote.death_age && !asymptote.bounces,
              "Rounded exponential asymptote caused a false finite-time collision");
    }
    const auto released =
        evaluate_particle_plane_motion({{0, 1, 0}, {0, -3, 0}}, {0, 2, 0}, 0, 3, floor(1, 0));
    check(!released.death_age && released.bounces == 1,
          "Outward force could not release zero rebound");
    const auto remaining = 3 - (3 - std::sqrt(5.)) / 2;
    close(released.state.position[1], remaining * remaining,
          "Outward release invented a resting body");
    const auto slide =
        evaluate_particle_plane_motion({{0, 1, 0}, {2, -2, 0}}, {}, 0, 1, floor(1, 0, .5));
    check(!slide.death_age && slide.bounces == 1,
          "Zero restitution without inward force killed a sliding particle");
    close(slide.state.position[0], 1.5, "Sliding contact lost tangential friction");
    close(slide.state.position[1], 0, "Sliding contact left the collision plane");
}
// Independent small-step RK4 ODE reference, with linearly localized crossings.
// It has no analytic integrals, extremum/root solver or shared motion call.
ParticleMotionState rk4(const ParticleMotionState& state, const std::array<double, 3>& acceleration,
                        double drag, double dt) {
    ParticleMotionState result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto v = state.velocity[axis], a = acceleration[axis];
        const auto k1 = a - drag * v, v2 = v + dt * k1 / 2, k2 = a - drag * v2,
                   v3 = v + dt * k2 / 2, k3 = a - drag * v3, v4 = v + dt * k3, k4 = a - drag * v4;
        result.position[axis] = state.position[axis] + dt * (v + 2 * v2 + 2 * v3 + v4) / 6;
        result.velocity[axis] = v + dt * (k1 + 2 * k2 + 2 * k3 + k4) / 6;
    }
    return result;
}
ParticlePlaneMotion reference(ParticleMotionState state, const std::array<double, 3>& acceleration,
                              double drag, const ParticleCollisionPlane& plane) {
    ParticlePlaneMotion result{state, 0, std::nullopt};
    constexpr unsigned steps = 62500;
    constexpr double dt = 1.25 / steps;
    for (unsigned step = 0; step < steps; ++step) {
        auto next = rk4(state, acceleration, drag, dt);
        if (next.position[1] < 0) {
            const auto hit = dt * state.position[1] / (state.position[1] - next.position[1]);
            state = rk4(state, acceleration, drag, hit);
            state.position[1] = 0;
            if (result.bounces == plane.bounce_limit) {
                result.state = state;
                result.death_age = step * dt + hit;
                return result;
            }
            ++result.bounces;
            state.velocity[1] *= -plane.restitution;
            state.velocity[0] *= 1 - plane.friction;
            state.velocity[2] *= 1 - plane.friction;
            next = rk4(state, acceleration, drag, dt - hit);
        }
        state = next;
    }
    result.state = state;
    return result;
}
void reference_profiles() {
    unsigned profiles = 0;
    for (double drag : {0., 1e-12, .25, 4., 64.})
        for (double velocity : {-3., -.5, 2.})
            for (double acceleration : {-4., 0., 4.})
                for (double restitution : {.25, .8}) {
                    const ParticleMotionState initial{{0, .5, 0}, {1, velocity, .3}};
                    const std::array force{.25, acceleration, -.1};
                    const auto plane = floor(3, restitution, .2);
                    const auto actual =
                        evaluate_particle_plane_motion(initial, force, drag, 1.25, plane);
                    const auto expected = reference(initial, force, drag, plane);
                    check(bool(actual.death_age) == bool(expected.death_age) &&
                              actual.bounces == expected.bounces,
                          "Analytic collision lifecycle differs from independent small-step ODE");
                    if (actual.death_age)
                        close(*actual.death_age, *expected.death_age,
                              "ODE reference death age differs", 1e-5);
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        close(actual.state.position[axis], expected.state.position[axis],
                              "ODE reference collision position differs", 1e-5);
                        close(actual.state.velocity[axis], expected.state.velocity[axis],
                              "ODE reference collision velocity differs", 1e-5);
                    }
                    const auto replay =
                        evaluate_particle_plane_motion(initial, force, drag, .1, plane);
                    (void)replay;
                    check(
                        actual.state ==
                            evaluate_particle_plane_motion(initial, force, drag, 1.25, plane).state,
                        "Backward/forward queries retained hidden collision history");
                    ++profiles;
                }
    std::cout << profiles << " independent RK4 collision profiles passed\n";
}
void admission() {
    for (double bad :
         {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto plane = floor();
        plane.radius = bad;
        reject([&] { (void)prepare_particle_collision_plane(plane); });
        plane = floor();
        plane.restitution = bad;
        reject([&] { (void)prepare_particle_collision_plane(plane); });
        plane = floor();
        plane.friction = bad;
        reject([&] { (void)prepare_particle_collision_plane(plane); });
    }
    auto plane = floor();
    plane.normal = {};
    reject([&] { (void)prepare_particle_collision_plane(plane); });
    plane = floor();
    plane.bounce_limit = 17;
    reject([&] { (void)prepare_particle_collision_plane(plane); });
    for (auto member : {&ParticleCollisionPlane::radius, &ParticleCollisionPlane::restitution,
                        &ParticleCollisionPlane::friction, &ParticleCollisionPlane::offset}) {
        plane = floor();
        plane.*member = 1e7;
        reject([&] { (void)prepare_particle_collision_plane(plane); });
    }
    for (double bad : {1e-13, 1e7, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        plane = floor();
        plane.normal = {bad, 0, 0};
        reject([&] { (void)prepare_particle_collision_plane(plane); });
    }
    for (double age : {-1., 30.1, std::numeric_limits<double>::quiet_NaN()})
        reject([&] { (void)evaluate_particle_plane_motion({}, {}, 0, age, floor()); });
}
void effect_projection() {
    ParticleEffectData free;
    free.version = 6;
    free.mesh = engine_primitive(0);
    free.emission = ParticleEmission::Burst;
    free.capacity = free.burst_count = 3;
    free.lifetime = 5;
    free.speed_min = free.speed_max = 0;
    free.cone_angle = 0;
    free.acceleration = {0, -2, 0};
    free.opacity_start = .75;
    free.opacity_end = .25;
    auto effect = free;
    effect.version = 11;
    effect.collision = floor(1);
    effect.collision->offset = -1;
    const auto encoded = encode_particle_effect(effect);
    check(encoded.at("profile") == "cpu_collision1" && decode_particle_effect(encoded) == effect &&
              particle_uses_sorted_alpha(effect) && particle_uses_opacity(effect) &&
              particle_uses_collision(effect) && !particle_uses_gpu(effect),
          "Collision values lost their explicit codec or inherited CPU appearance contract");
    for (double age : {0., .25, std::nextafter(1., 0.)})
        check(sample_particle_effect(effect, age, 17) == sample_particle_effect(free, age, 17),
              "Pre-contact sampling changed seeded birth, motion or lifetime appearance");
    const auto bounced = sample_particle_effect(effect, 2, 17);
    const auto original = sample_particle_effect(free, 2, 17);
    check(bounced.size() == 3 && original.size() == 3,
          "Plane response changed original birth count");
    for (unsigned index = 0; index < 3; ++index) {
        const auto& actual = bounced[index];
        close(actual.position[1], 0, "Effect sampler did not consume plane response");
        check(actual.birth == original[index].birth && actual.age == original[index].age &&
                  actual.size == original[index].size && actual.color == original[index].color &&
                  actual.opacity == original[index].opacity,
              "Collision restarted lifetime appearance or rerolled a transient birth");
    }
    check(sample_particle_effect(effect, 3, 17).empty(),
          "Exhausted births were drawn after their death");
    effect.collision->bounce_limit = 0;
    check(sample_particle_effect(effect, 1, 17).empty(),
          "Exact impact did not apply kill-on-contact");
    auto wrong = encoded;
    wrong["collision"]["bounce_limit"] = 1.5;
    reject([&] { (void)decode_particle_effect(wrong); });
    wrong = encoded;
    wrong["collision"]["normal"] = {0, 0, 0};
    reject([&] { (void)decode_particle_effect(wrong); });
    wrong = encoded;
    wrong["collision"]["extra"] = true;
    reject([&] { (void)decode_particle_effect(wrong); });
    wrong = encoded;
    wrong.erase("collision");
    reject([&] { (void)decode_particle_effect(wrong); });
    wrong = encode_particle_effect(free);
    wrong["collision"] = encoded.at("collision");
    reject([&] { (void)decode_particle_effect(wrong); });
    effect.version = 6;
    reject([&] { validate_particle_effect(effect); });
}
void maximum_speed_births() {
    ParticleEffectData effect;
    effect.version = 6;
    effect.mesh = engine_primitive(0);
    effect.emission = ParticleEmission::Burst;
    effect.capacity = effect.burst_count = 256;
    effect.speed_min = effect.speed_max = 1000;
    effect.direction = {1, 1e-7, 0};
    effect.cone_angle = .00001;
    effect.acceleration = {};
    const auto original = sample_particle_effect(effect, 0, 0, 256);
    effect.version = 11;
    effect.collision = ParticleCollisionPlane{{0, 1, 0}, -1000000, 0, 1, 0, 16};
    check(sample_particle_effect(effect, 0, 0, 256) == original,
          "A valid maximum-speed birth was rejected or clamped after cone-frame rounding");
    auto invalid = ParticleMotionState{};
    invalid.velocity[0] = std::numeric_limits<double>::infinity();
    reject([&] { (void)evaluate_particle_plane_motion(invalid, {}, 0, 0, floor()); });
}
} // namespace
int main() {
    try {
        exact_contacts();
        plane_geometry();
        grazing_and_drag();
        reference_profiles();
        admission();
        effect_projection();
        maximum_speed_births();
        std::cout << "Particle analytic plane motion checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
