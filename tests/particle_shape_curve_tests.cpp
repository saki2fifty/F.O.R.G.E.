#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid extended particle values accepted");
}
ParticleEffectData fixture() {
    ParticleEffectData data;
    data.mesh = engine_primitive(0);
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1024;
    data.speed_min = data.speed_max = 0;
    data.acceleration = {0, 0, 0};
    data.lifetime = 4;
    data.version = 2;
    return data;
}
void shapes() {
    auto data = fixture();
    data.shape_size = {2, 4, 6};
    data.shape_radius = 2;
    data.direction = {1, 2, -2};
    for (const auto shape :
         {ParticleSpawnShape::Box, ParticleSpawnShape::Sphere, ParticleSpawnShape::Disc}) {
        data.shape = shape;
        std::array<double, 3> sums{}, squares{};
        double radius_squared = 0;
        for (unsigned seed = 0; seed < 16; ++seed) {
            const auto born = sample_particle_effect(data, 0, seed);
            const auto later = sample_particle_effect(data, 3.5, seed);
            require(born.size() == 1024 && later.size() == born.size(),
                    "Shape sampling changed birth budget");
            for (unsigned at = 0; at < born.size(); ++at) {
                const auto& p = born[at].position;
                require(p == later[at].position && born[at].birth == later[at].birth,
                        "A particle origin was resampled from observation time");
                const double squared = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
                radius_squared += squared;
                if (shape == ParticleSpawnShape::Box)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        require(std::abs(p[axis]) <= data.shape_size[axis] * .5,
                                "Box origin exceeds full dimensions");
                else {
                    require(squared <= 4 + 1e-12, "Round origin exceeds radius");
                    if (shape == ParticleSpawnShape::Disc)
                        require(std::abs(p[0] + 2 * p[1] - 2 * p[2]) < 1e-12,
                                "Disc is not perpendicular to the emission axis");
                }
                for (unsigned axis = 0; axis < 3; ++axis) {
                    sums[axis] += p[axis];
                    squares[axis] += p[axis] * p[axis];
                }
            }
        }
        constexpr double count = 16 * 1024;
        for (unsigned axis = 0; axis < 3; ++axis)
            require(std::abs(sums[axis] / count) < .04, "Origin distribution is not centered");
        if (shape == ParticleSpawnShape::Box)
            for (unsigned axis = 0; axis < 3; ++axis)
                require(std::abs(squares[axis] / count -
                                 data.shape_size[axis] * data.shape_size[axis] / 12) < .07,
                        "Uniform box origin fails its independent second-moment oracle");
        else
            require(std::abs(radius_squared / count -
                             (shape == ParticleSpawnShape::Sphere ? 12. / 5 : 2)) < .05,
                    "Uniform sphere/disc origin fails its volume/area moment oracle");
    }
}
void drag() {
    auto data = fixture();
    data.capacity = data.burst_count = 1;
    data.cone_angle = 0;
    data.speed_min = data.speed_max = 3;
    data.direction = {0, 1, 0};
    data.acceleration = {2, -4, 1};
    const auto ballistic = sample_particle_effect(data, .7, 9)[0];
    require(std::abs(ballistic.position[1] - (3 * .7 - 2 * .7 * .7)) < 1e-12,
            "Drag zero changed ballistic motion");
    // Independent RK4 integration of dv/dt = acceleration - drag*velocity.
    for (const double resistance : {1e-14, .0001, 1., 20., 64.}) {
        data.drag = resistance;
        std::array<double, 3> position{}, velocity{0, 3, 0};
        constexpr unsigned steps = 8192;
        constexpr double age = 1.25, dt = age / steps;
        for (unsigned tick = 0; tick < steps; ++tick)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto acceleration = [&](double v) {
                    return data.acceleration[axis] - resistance * v;
                };
                const auto v = velocity[axis], k1 = acceleration(v), v2 = v + dt * k1 / 2,
                           k2 = acceleration(v2), v3 = v + dt * k2 / 2, k3 = acceleration(v3),
                           v4 = v + dt * k3, k4 = acceleration(v4);
                position[axis] += dt * (v + 2 * v2 + 2 * v3 + v4) / 6;
                velocity[axis] += dt * (k1 + 2 * k2 + 2 * k3 + k4) / 6;
            }
        const auto actual = sample_particle_effect(data, age, 9)[0];
        for (unsigned axis = 0; axis < 3; ++axis)
            require(std::abs(actual.position[axis] - position[axis]) < 2e-10 &&
                        std::abs(actual.velocity[axis] - velocity[axis]) < 2e-10,
                    "Analytic drag differs from independent RK4 or loses the tiny-drag limit");
    }
}
void curves_and_versions() {
    auto data = fixture();
    data.capacity = data.burst_count = 1;
    data.size_curve = {{0, .1, true}, {.25, 2, false}, {1, .5, false}};
    data.color_gradient = {{0, {1, 0, 0}, false}, {.5, {0, 1, 0}, true}, {1, {0, 0, 1}, false}};
    auto at = [&](double t) { return sample_particle_effect(data, t * 4, 1).at(0); };
    require(at(0).size == .1 && at(.249).size == .1 && at(.25).size == 2,
            "Size Step segment changed at the wrong normalized lifetime");
    require(at(.5).size == 1.5 && at(.125).color == std::array{.75f, .25f, 0.f} &&
                at(.75).color == std::array{0.f, 1.f, 0.f},
            "Size/color lifetime interpolation differs from independent values");
    const auto encoded = encode_particle_effect(data);
    require(decode_particle_effect(encoded) == data, "Version2 values lost codec roundtrip");
    auto bad = encoded;
    bad["size_curve"][1]["time"] = 0;
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["color_gradient"][0]["value"][1] = 2;
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["size_curve"][0]["interpolation"] = "cubic";
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["size_curve"][2]["id"] = "unexpected";
    reject([&] { decode_particle_effect(bad); });
    for (const auto version : {0, 3}) {
        bad = encoded;
        bad["version"] = version;
        reject([&] { decode_particle_effect(bad); });
    }
    auto legacy = fixture();
    legacy.version = 1;
    legacy.speed_min = .5;
    legacy.speed_max = 3;
    legacy.cone_angle = 65;
    auto extended = legacy;
    extended.version = 2;
    for (unsigned seed : {0u, 1u, 17u, UINT32_MAX})
        require(sample_particle_effect(legacy, .8, seed) ==
                    sample_particle_effect(extended, .8, seed),
                "Version2 Point defaults changed the exact legacy samples or random sequence");
    require(decode_particle_effect(encode_particle_effect(legacy)) == legacy,
            "Legacy value codec changed");
    extended.drag = -1;
    reject([&] { validate_particle_effect(extended); });
    legacy.drag = 1;
    reject([&] { validate_particle_effect(legacy); });
    extended = fixture();
    extended.size_curve = {{0, .1, false}};
    reject([&] { validate_particle_effect(extended); });
    extended = fixture();
    extended.shape = ParticleSpawnShape(5);
    reject([&] { validate_particle_effect(extended); });
    extended = fixture();
    extended.shape_radius = std::numeric_limits<double>::infinity();
    reject([&] { validate_particle_effect(extended); });
}
} // namespace
int main() {
    try {
        shapes();
        drag();
        curves_and_versions();
        std::cout << "Particle origin geometry/moments, independent drag ODE, lifetime stops and "
                     "legacy samples passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
