#pragma once
#include "particle_gpu_plan.hpp"
#include "particle_motion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace forge::particle_gpu_test {
struct Random {
    std::uint32_t state;
    Random(std::uint32_t seed, std::uint64_t ordinal) {
        std::uint32_t hash = 2166136261u;
        for (unsigned byte = 0; byte < 12; ++byte) {
            const auto value = byte < 4 ? std::uint8_t(seed >> (byte * 8))
                                        : std::uint8_t(ordinal >> ((byte - 4) * 8));
            hash = (hash ^ value) * 16777619u;
        }
        state = hash % 2147483646u + 1u;
    }
    double unit() {
        state = std::uint32_t(std::uint64_t(state) * 48271u % 2147483647u);
        return double(state >> 7) / 16777216.;
    }
};

inline ParticleMotionState spawn_state(const ParticleGpuValues& p, unsigned id) {
    const auto ordinal = ((std::uint64_t(p.births[2]) << 32) | p.births[1]) + id;
    Random random(p.births[3], ordinal);
    const double c = 1 - random.unit() * (1 - p.axis[3]), s = std::sqrt(std::max(0., 1 - c * c)),
                 phi = random.unit() * 6.2831853071795864769;
    const double speed = p.side[3] + random.unit() * (p.up[3] - p.side[3]);
    ParticleMotionState result;
    auto& origin = result.position;
    auto& initial = result.velocity;
    for (unsigned i = 0; i < 3; ++i)
        initial[i] =
            speed * (p.axis[i] * c + p.side[i] * s * std::cos(phi) + p.up[i] * s * std::sin(phi));
    if (p.modes[0] == 1) {
        for (unsigned i = 0; i < 3; ++i)
            origin[i] = (random.unit() - .5) * p.shape[i];
    } else if (p.modes[0] == 2) {
        const double radius = p.shape[3] * std::cbrt(random.unit()), y = 2 * random.unit() - 1,
                     radial = std::sqrt(std::max(0., 1 - y * y)),
                     angle = random.unit() * 6.2831853071795864769;
        origin = {radius * radial * std::cos(angle), radius * y, radius * radial * std::sin(angle)};
    } else if (p.modes[0] == 3) {
        const double radius = p.shape[3] * std::sqrt(random.unit()),
                     angle = random.unit() * 6.2831853071795864769;
        for (unsigned i = 0; i < 3; ++i)
            origin[i] = radius * (p.side[i] * std::cos(angle) + p.up[i] * std::sin(angle));
    }
    return result;
}
} // namespace forge::particle_gpu_test
