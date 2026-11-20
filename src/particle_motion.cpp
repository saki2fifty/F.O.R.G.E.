#include "particle_motion.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace forge {
namespace {
struct MotionFactors {
    double age, damping = 1, velocity_integral, acceleration_integral;
    bool undamped;
    MotionFactors(double drag, double seconds)
        : age(seconds), velocity_integral(seconds), acceleration_integral(.5 * seconds * seconds),
          undamped(drag == 0) {
        if (drag > 0) {
            const auto x = drag * age;
            damping = std::exp(-x);
            velocity_integral = -std::expm1(-x) / drag;
            // Integral of the velocity response: stable near zero drag*age.
            acceleration_integral =
                x < .001
                    ? age * age * (.5 + x * (-1. / 6 + x * (1. / 24 + x * (-1. / 120 + x / 720))))
                    : (age - velocity_integral) / drag;
        }
    }
    std::array<double, 2> apply(double position, double velocity, double acceleration) const {
        if (undamped)
            return {position + velocity * age + .5 * acceleration * age * age,
                    velocity + acceleration * age};
        return {position + velocity * velocity_integral + acceleration * acceleration_integral,
                velocity * damping + acceleration * velocity_integral};
    }
};
double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void range(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::runtime_error("Particle collision plane value is outside its finite range");
}
template <class Predicate> double bisect(double low, double high, Predicate positive) {
    for (unsigned iteration = 0; iteration < 80; ++iteration) {
        const auto middle = std::midpoint(low, high);
        if (middle == low || middle == high)
            break;
        if (positive(middle))
            low = middle;
        else
            high = middle;
    }
    return high;
}
std::optional<double> incoming_contact(double distance, double velocity, double acceleration,
                                       double drag, double age) {
    if (distance == 0 && (velocity < 0 || (velocity == 0 && acceleration < 0)))
        return 0;
    if (age == 0)
        return std::nullopt;
    // With no normal force, linear drag can approach the plane without ever
    // reaching it. expm1 may round that finite excursion to its asymptote.
    if (drag > 0 && acceleration == 0 && velocity < 0 && distance + velocity / drag >= 0)
        return std::nullopt;
    const auto at = [&](double time) {
        return MotionFactors(drag, time).apply(distance, velocity, acceleration);
    };
    auto end = age;
    auto last = at(end);
    // Normal velocity approaches one terminal value monotonically. Only an
    // incoming-to-outgoing sign change can conceal a crossing before this query.
    if (velocity < 0 && last[1] > 0) {
        end = bisect(0, end, [&](double time) { return at(time)[1] < 0; });
        last = at(end);
        if (last[0] >= 0) // A tangent touch does not enter the blocked half-space.
            return std::nullopt;
    } else if (last[0] > 0 || (last[0] == 0 && last[1] >= 0))
        return std::nullopt;
    if (last[0] == 0)
        return end;
    return bisect(0, end, [&](double time) { return at(time)[0] > 0; });
}
} // namespace

ParticleMotionState evaluate_particle_motion(const ParticleMotionState& initial,
                                             const std::array<double, 3>& acceleration, double drag,
                                             double age) {
    const MotionFactors factors(drag, age);
    ParticleMotionState result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto values =
            factors.apply(initial.position[axis], initial.velocity[axis], acceleration[axis]);
        result.position[axis] = values[0];
        result.velocity[axis] = values[1];
    }
    return result;
}
void validate_particle_collision_plane(const ParticleCollisionPlane& plane) {
    for (double value : plane.normal)
        range(value, -1e6, 1e6);
    const auto length = std::hypot(plane.normal[0], plane.normal[1], plane.normal[2]);
    if (length < 1e-12)
        throw std::runtime_error("Particle collision plane normal must be nonzero");
    range(plane.offset, -1e6, 1e6);
    range(plane.radius, 0, 1000);
    range(plane.restitution, 0, 1);
    range(plane.friction, 0, 1);
    if (plane.bounce_limit > 16)
        throw std::runtime_error("Particle collision plane permits at most 16 bounces");
}
ParticleCollisionPlane prepare_particle_collision_plane(const ParticleCollisionPlane& plane) {
    validate_particle_collision_plane(plane);
    const auto length = std::hypot(plane.normal[0], plane.normal[1], plane.normal[2]);
    auto prepared = plane;
    for (auto& value : prepared.normal)
        value /= length;
    return prepared;
}
ParticlePlaneMotion evaluate_particle_plane_motion(const ParticleMotionState& initial,
                                                   const std::array<double, 3>& acceleration,
                                                   double drag, double age,
                                                   const ParticleCollisionPlane& plane) {
    range(age, 0, 30);
    range(drag, 0, 64);
    for (unsigned axis = 0; axis < 3; ++axis) {
        range(initial.position[axis], -1e10, 1e10);
        // Source admission bounds the scalar speed. Cone-frame rounding can
        // place a component a few ULPs above that bound without changing the
        // admitted birth; do not apply the scalar limit a second time here.
        if (!std::isfinite(initial.velocity[axis]))
            throw std::runtime_error("Particle collision birth velocity must be finite");
        range(acceleration[axis], -10000, 10000);
    }
    ParticlePlaneMotion result{initial, 0, std::nullopt};
    double distance = dot(initial.position, plane.normal) - plane.offset - plane.radius;
    if (distance < 0) {
        result.death_age = 0;
        return result;
    }
    double normal_velocity = dot(initial.velocity, plane.normal), elapsed = 0;
    const auto normal_acceleration = dot(acceleration, plane.normal);
    for (;;) {
        const auto remaining = age - elapsed;
        const auto contact =
            incoming_contact(distance, normal_velocity, normal_acceleration, drag, remaining);
        if (!contact) {
            result.state = evaluate_particle_motion(result.state, acceleration, drag, remaining);
            return result;
        }
        result.state = evaluate_particle_motion(result.state, acceleration, drag, *contact);
        const auto incoming =
            MotionFactors(drag, *contact).apply(distance, normal_velocity, normal_acceleration)[1];
        elapsed = std::min(age, elapsed + *contact);
        const auto correction =
            plane.offset + plane.radius - dot(result.state.position, plane.normal);
        for (unsigned axis = 0; axis < 3; ++axis)
            result.state.position[axis] += plane.normal[axis] * correction;
        if (result.bounces == plane.bounce_limit || (incoming == 0 && normal_acceleration <= 0)) {
            result.death_age = elapsed;
            return result;
        }
        normal_velocity = -plane.restitution * incoming;
        ++result.bounces;
        for (unsigned axis = 0; axis < 3; ++axis)
            result.state.velocity[axis] =
                (result.state.velocity[axis] - plane.normal[axis] * incoming) *
                    (1 - plane.friction) +
                plane.normal[axis] * normal_velocity;
        if (normal_velocity == 0 && normal_acceleration < 0) {
            result.death_age = elapsed;
            return result;
        }
        distance = 0;
    }
}
} // namespace forge
