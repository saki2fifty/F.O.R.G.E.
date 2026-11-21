#include "particle_scene_motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace forge {
namespace {
double dot(const Double3& a, const Double3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void range(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("Particle Scene motion value is outside its finite range");
}
void valid_motion(const ParticleMotionState& state) {
    for (unsigned axis = 0; axis < 3; ++axis) {
        range(state.position[axis], -1e10, 1e10);
        range(state.velocity[axis], -1e10, 1e10);
    }
}
} // namespace
ParticleSceneStep advance_particle_scene_motion(const ParticleSceneState& original,
                                                const Double3& acceleration, double drag, double dt,
                                                const ParticleSceneCollision& settings,
                                                const ParticleSceneQuery& query, bool initial_probe,
                                                bool planar_2d) {
    range(dt, 0, 30);
    range(drag, 0, 64);
    if (planar_2d)
        validate_particle_scene_collision_2d(settings);
    else
        validate_particle_scene_collision(settings);
    if (original.bounces > settings.bounce_limit)
        throw std::runtime_error("Particle Scene rebound count is invalid");
    valid_motion(original.motion);
    for (auto value : acceleration)
        range(value, -1e10, 1e10);
    ParticleSceneStep result{original, {{0, original.motion}}, {}, 0, dt};
    if (planar_2d) {
        result.acceleration = acceleration;
        result.drag = drag;
        result.planar_2d = true;
    }
    if (!original.alive) {
        result.death = 0;
        return result;
    }
    double elapsed = 0;
    bool pending_probe = initial_probe && dt == 0;
    while (elapsed < dt || pending_probe) {
        pending_probe = false;
        if (++result.queries > particle_scene_birth_query_limit)
            throw std::runtime_error(
                planar_2d
                    ? "Particle Scene motion exceeds 256 circle queries per birth and fixed update"
                    : "Particle Scene motion exceeds 256 sphere queries per birth and fixed "
                      "update");
        auto& state = result.state;
        double magnitude = 1;
        Double3 second_derivative;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!planar_2d || axis < 2)
                magnitude = std::max(magnitude, std::abs(state.motion.position[axis]));
            second_derivative[axis] = acceleration[axis] - drag * state.motion.velocity[axis];
        }
        // For constant acceleration and nonnegative drag, the acceleration vector
        // is exp(-drag*t) times its initial value. Its norm cannot increase.
        // A segment's chord error is bounded by norm(second_derivative)*h*h/8.
        const auto curvature = planar_2d ? std::hypot(second_derivative[0], second_derivative[1])
                                         : std::hypot(second_derivative[0], second_derivative[1],
                                                      second_derivative[2]);
        const auto tolerance = std::min(.001, settings.radius * .01);
        double interval = dt - elapsed;
        if (curvature > 0)
            interval = std::min({interval, 1. / 120, std::sqrt(8 * tolerance / curvature)});
        if ((!initial_probe || dt != 0) && (!(interval > 0) || elapsed + interval == elapsed))
            throw std::runtime_error(
                "Particle Scene motion cannot advance at the supplied time precision");
        const auto start = state.motion;
        auto endpoint = evaluate_particle_motion(start, acceleration, drag, interval);
        valid_motion(endpoint);
        Double3 displacement;
        for (unsigned axis = 0; axis < 3; ++axis)
            displacement[axis] = endpoint.position[axis] - start.position[axis];
        const auto hit = query(start.position, displacement, settings.radius);
        if (!hit) {
            state.motion = endpoint;
            elapsed = interval == dt - elapsed ? dt : elapsed + interval;
            result.knots.push_back({elapsed, state.motion});
            continue;
        }
        range(hit->fraction, 0, 1);
        const auto length = std::hypot(hit->normal[0], hit->normal[1], hit->normal[2]);
        if (planar_2d && hit->normal[2] != 0)
            throw std::runtime_error("XY Scene query returned a nonplanar contact normal");
        if (planar_2d && hit->fraction == 0 && length == 0) {
            // The existing native circle query explicitly reports initial overlap
            // (including touch) without a normal. End it; do not invent a normal.
            for (double value : hit->position)
                range(value, -1e10, 1e10);
            state.alive = false;
            result.death = elapsed;
            return result;
        }
        if (!std::isfinite(length) || length <= 0)
            throw std::runtime_error("Particle Scene query returned an invalid contact normal");
        Double3 normal, relative;
        for (unsigned axis = 0; axis < 3; ++axis) {
            range(hit->position[axis], -1e10, 1e10);
            normal[axis] = hit->normal[axis] / length;
            relative[axis] = start.position[axis] - hit->position[axis];
        }
        // XY response keeps a 5 mm departure skin: the native cast can classify
        // a smaller positive gap as initial contact, particularly at its minimum radius.
        const double bias = std::max({settings.radius * 1e-4,
                                      32 * std::numeric_limits<double>::epsilon() * magnitude,
                                      planar_2d ? .005 : 0.});
        if (hit->fraction == 0 && dot(relative, normal) - settings.radius < -2 * bias) {
            state.alive = false;
            result.death = elapsed;
            return result;
        }
        double fraction = hit->fraction;
        if (planar_2d && fraction > 0) {
            // Native contact tolerance can place the reported cast center inside
            // the circle radius. Clip to the target witness's supporting plane
            // before response; keep the original chord and its time correspondence.
            const double gap = dot(relative, normal) - settings.radius;
            const double approach = dot(displacement, normal);
            if (gap >= 0 && approach < 0)
                fraction = std::min(fraction, gap / -approach);
        }
        const auto travel_time = interval * fraction;
        state.motion = evaluate_particle_motion(start, acceleration, drag, travel_time);
        for (unsigned axis = 0; axis < (planar_2d ? 2u : 3u); ++axis)
            state.motion.position[axis] = start.position[axis] + displacement[axis] * fraction;
        elapsed = travel_time == dt - elapsed ? dt : elapsed + travel_time;
        result.knots.push_back({elapsed, state.motion});
        const auto incoming = dot(state.motion.velocity, normal);
        if ((incoming == 0 && dot(acceleration, normal) < 0) ||
            (incoming < 0 && state.bounces == settings.bounce_limit)) {
            state.alive = false;
            result.death = elapsed;
            return result;
        }
        if (incoming < 0) {
            for (unsigned axis = 0; axis < (planar_2d ? 2u : 3u); ++axis)
                state.motion.velocity[axis] =
                    (state.motion.velocity[axis] - incoming * normal[axis]) *
                        (1 - settings.friction) -
                    incoming * settings.restitution * normal[axis];
            ++state.bounces;
            if (settings.restitution == 0 && dot(acceleration, normal) < 0) {
                state.alive = false;
                result.death = elapsed;
                return result;
            }
        }
        // Keep the body in the next query: one compound/concave body can contain
        // another surface. Only a small positional departure bias is introduced.
        for (unsigned axis = 0; axis < 3; ++axis)
            state.motion.position[axis] += normal[axis] * bias;
        valid_motion(state.motion);
        result.knots.push_back({elapsed, state.motion});
    }
    return result;
}
std::optional<ParticleMotionState> sample_particle_scene_motion(const ParticleSceneStep& step,
                                                                double time) {
    if (step.knots.empty() || !std::isfinite(time) || time < 0 || time > step.duration)
        throw std::runtime_error("Particle Scene trace sample is outside its fixed interval");
    if (step.death && time >= *step.death)
        return {};
    auto next = std::upper_bound(step.knots.begin(), step.knots.end(), time,
                                 [](double t, const auto& knot) { return t < knot.time; });
    if (next == step.knots.end())
        return step.knots.back().motion;
    const auto& previous = *(next - 1);
    const auto weight = (time - previous.time) / (next->time - previous.time);
    auto value = previous.motion;
    for (unsigned axis = 0; axis < 3; ++axis) {
        value.position[axis] =
            std::lerp(previous.motion.position[axis], next->motion.position[axis], weight);
        value.velocity[axis] =
            std::lerp(previous.motion.velocity[axis], next->motion.velocity[axis], weight);
    }
    if (step.planar_2d) {
        const auto exact = evaluate_particle_motion(previous.motion, step.acceleration, step.drag,
                                                    time - previous.time);
        range(exact.position[2], -1e10, 1e10);
        range(exact.velocity[2], -1e10, 1e10);
        value.position[2] = exact.position[2];
        value.velocity[2] = exact.velocity[2];
    }
    return value;
}
} // namespace forge
