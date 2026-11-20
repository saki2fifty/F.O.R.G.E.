#include <cmath>
#include <forge/particle_effect.hpp>
#include <stdexcept>

namespace forge {
namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error(
            "Particle World capture has invalid types, values, order or budget");
}
void range(double value, double low, double high) {
    require(std::isfinite(value) && value >= low && value <= high);
}
double number(const nlohmann::json& value) {
    require(value.is_number());
    return value.get<double>();
}
template <class T, std::size_t N>
void vector(const nlohmann::json& value, std::array<T, N>& result) {
    require(value.is_array() && value.size() == N);
    for (std::size_t i = 0; i < N; ++i)
        result[i] = T(number(value[i]));
}
} // namespace
void validate_particle_world_samples(std::span<const ParticleWorldSample> samples) {
    require(samples.size() <= particle_scene_collision_capacity);
    std::optional<std::uint64_t> previous;
    for (const auto& value : samples) {
        const auto& sample = value.sample;
        // Source time<=1e9 and rate<=256 bound all admitted CPU birth ordinals.
        require(sample.birth < 256000000000ULL && (!previous || sample.birth > *previous));
        previous = sample.birth;
        range(sample.age, 0, 30);
        range(sample.size, .001, 100);
        range(sample.opacity, 0, 1);
        for (unsigned axis = 0; axis < 3; ++axis) {
            range(sample.position[axis], -1e10, 1e10);
            range(sample.velocity[axis], -1e10, 1e10);
            range(sample.color[axis], 0, 1);
        }
        for (auto coefficient : value.basis)
            range(coefficient, -1e10, 1e10);
    }
}
nlohmann::json encode_particle_world_samples(std::span<const ParticleWorldSample> samples) {
    validate_particle_world_samples(samples);
    auto result = nlohmann::json::array();
    for (const auto& value : samples) {
        const auto& sample = value.sample;
        result.push_back({{"birth", sample.birth},
                          {"age", sample.age},
                          {"position", sample.position},
                          {"velocity", sample.velocity},
                          {"size", sample.size},
                          {"color", sample.color},
                          {"opacity", sample.opacity},
                          {"basis", value.basis}});
    }
    return result;
}
std::vector<ParticleWorldSample> decode_particle_world_samples(const nlohmann::json& values) {
    require(values.is_array() && values.size() <= particle_scene_collision_capacity);
    std::vector<ParticleWorldSample> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        require(value.is_object() && value.size() == 8 && value.at("birth").is_number_unsigned());
        ParticleWorldSample decoded;
        auto& sample = decoded.sample;
        sample.birth = value.at("birth").get<std::uint64_t>();
        sample.age = number(value.at("age"));
        sample.size = number(value.at("size"));
        sample.opacity = number(value.at("opacity"));
        vector(value.at("position"), sample.position);
        vector(value.at("velocity"), sample.velocity);
        vector(value.at("color"), sample.color);
        vector(value.at("basis"), decoded.basis);
        result.push_back(std::move(decoded));
    }
    validate_particle_world_samples(result);
    return result;
}
} // namespace forge
