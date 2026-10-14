#include <cmath>
#include <forge/particle_effect.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected invalid particle input rejection");
}
ParticleEffectData reference() {
    ParticleEffectData result;
    result.mesh.id = AssetId::generate();
    result.spawn_rate = 4;
    result.lifetime = 1;
    result.capacity = 4;
    result.burst_count = 4;
    result.cone_angle = 0;
    result.speed_min = result.speed_max = 2;
    result.acceleration = {0, -4, 0};
    result.size_start = 1;
    result.size_end = .5;
    result.color_start = {1, 0, 0};
    result.color_end = {0, 0, 1};
    return result;
}
void equations() {
    auto data = reference();
    require(sample_particle_effect(data, 0, 1).empty(),
            "Continuous effect spawned before first interval");
    require(sample_particle_effect(data, .25, 1).size() == 1, "First birth boundary wrong");
    auto samples = sample_particle_effect(data, .875, 1);
    require(samples.size() == 3, "Continuous birth count wrong");
    for (unsigned i = 0; i < 3; ++i) {
        const auto age = .875 - (i + 1) * .25;
        const auto& sample = samples[i];
        require(sample.birth == i && sample.age == age && sample.position[0] == 0 &&
                    sample.position[2] == 0,
                "Birth identity or local axis wrong");
        require(std::abs(sample.position[1] - (2 * age - 2 * age * age)) < 1e-12 &&
                    std::abs(sample.velocity[1] - (2 - 4 * age)) < 1e-12,
                "Constant acceleration equation wrong");
        require(sample.size == 1 - .5 * age && std::abs(sample.color[0] - (1 - age)) < 1e-6 &&
                    std::abs(sample.color[2] - age) < 1e-6,
                "Lifetime appearance interpolation wrong");
    }
    require(sample_particle_effect(data, 1.25, 1).front().birth == 1,
            "Dead birth survived exact lifetime");
    reject([&] { sample_particle_effect(data, 1, 1, 3); });
    require(sample_particle_effect(data, 0, 1, 0).empty(), "Empty budget rejected empty result");
    data.emission = ParticleEmission::Burst;
    require(sample_particle_effect(data, 0, 1).size() == 4 &&
                sample_particle_effect(data, 1, 1).empty(),
            "Burst start/end boundary wrong");
    data.emission = ParticleEmission::Continuous;
    data.capacity = 1;
    data.spawn_rate = 1;
    require(sample_particle_effect(data, 1, 1).size() == 1,
            "Inactive burst count prevented a valid small continuous effect");
    data.emission = ParticleEmission::Burst;
    reject([&] { sample_particle_effect(data, 0, 1); });
}
void seeds_and_windows() {
    auto data = reference();
    data.capacity = 256;
    data.burst_count = 256;
    data.emission = ParticleEmission::Burst;
    data.cone_angle = 40;
    data.speed_min = 1;
    data.speed_max = 3;
    data.acceleration = {0, 0, 0};
    auto a = sample_particle_effect(data, .3, 17), b = sample_particle_effect(data, .3, 17),
         c = sample_particle_effect(data, .3, 18);
    require(a == b && a != c, "Seeded visual sample not repeatable or seed had no effect");
    for (const auto& sample : a) {
        const auto speed = std::hypot(sample.velocity[0], sample.velocity[1], sample.velocity[2]);
        require(speed >= 1 && speed <= 3 &&
                    sample.velocity[1] / speed >= std::cos(40 * std::acos(-1.) / 180) - 1e-12,
                "Velocity escaped configured cone/speed");
    }
    data.emission = ParticleEmission::Continuous;
    data.spawn_rate = 7.5;
    data.lifetime = .3;
    for (unsigned step = 0; step < 2000; ++step) {
        const auto time = step * .005;
        unsigned expected = 0;
        for (unsigned birth = 1; birth <= 100; ++birth) {
            const auto phase_age = time * data.spawn_rate - birth;
            expected += phase_age >= 0 && phase_age < data.lifetime * data.spawn_rate;
        }
        require(sample_particle_effect(data, time, 1).size() == expected,
                "Bounded window differs from brute-force birth oracle");
    }
    data = reference();
    data.spawn_rate = 256;
    data.lifetime = .03125;
    data.capacity = data.burst_count = 8;
    auto late = sample_particle_effect(data, 1e9, 7);
    require(late.size() == 8 && late.front().birth > UINT32_MAX,
            "Large sample time walked/truncated historical births");
}
void admission() {
    auto data = reference();
    auto encoded = encode_particle_effect(data);
    require(decode_particle_effect(encoded) == data,
            "Known effect values changed during codec round trip");
    for (auto field : {"spawn_rate", "lifetime", "speed_min", "cone_angle"}) {
        auto bad = encoded;
        bad[field] = true;
        reject([&] { decode_particle_effect(bad); });
    }
    for (const auto count : {-1., 0., 1.5, 1025.}) {
        auto bad = encoded;
        bad["capacity"] = count;
        reject([&] { decode_particle_effect(bad); });
    }
    auto bad = encoded;
    bad["color_start"] = {-1e-50, 0, 0};
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["direction"] = {0, 0, 0};
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["profile"] = "gpu";
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["unimplemented_feature"] = 1;
    reject([&] { decode_particle_effect(bad); });
    bad = encoded;
    bad["spawn_rate"] = 5;
    reject([&] { decode_particle_effect(bad); });
    for (auto time : {-1., 1e9 + 1, std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()})
        reject([&] { sample_particle_effect(data, time, 1); });
}
} // namespace
int main() {
    try {
        equations();
        seeds_and_windows();
        admission();
        std::cout << "Particle CPU equations, identity, windows, seeds and admission passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
