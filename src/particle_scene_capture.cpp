#include "particle_birth.hpp"
#include "particle_path.hpp"
#include <algorithm>
#include <cmath>
#include <forge/particle_effect.hpp>
#include <limits>
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
void validate_particle_world_paths(const ParticleWorldPaths& values) {
    const auto& paths = values.paths;
    require(paths.paths.size() <= particle_scene_collision_capacity &&
            values.width_scales.size() == paths.paths.size() &&
            paths.points.size() >= paths.paths.size() &&
            paths.segment_count() <= particle_scene_capacity);
    std::size_t consumed = 0;
    std::optional<std::uint64_t> previous;
    for (std::size_t i = 0; i < paths.paths.size(); ++i) {
        const auto& path = paths.paths[i];
        require(path.birth < 256000000000ULL && (!previous || path.birth > *previous) &&
                path.first == consumed && path.count >= 2 &&
                path.count <= particle_path_segment_limit + 1 &&
                path.first <= paths.points.size() &&
                path.count <= paths.points.size() - path.first);
        previous = path.birth;
        range(values.width_scales[i], 0, 2e10);
        double age = -1;
        for (const auto& point : std::span(paths.points).subspan(path.first, path.count)) {
            require(point.birth == path.birth && point.age > age &&
                    point.velocity == std::array<double, 3>{});
            range(point.age, 0, 30);
            range(point.size, .001, 100);
            range(point.opacity, 0, 1);
            for (unsigned axis = 0; axis < 3; ++axis) {
                range(point.position[axis], -1e10, 1e10);
                range(point.color[axis], 0, 1);
            }
            age = point.age;
        }
        consumed += path.count;
    }
    require(consumed == paths.points.size());
}
std::size_t particle_world_capture_birth_count(std::span<const ParticleWorldSample> heads,
                                               const ParticleWorldPaths* trails) {
    if (!trails)
        return heads.size();
    std::size_t head = 0, path = 0, count = 0;
    const auto& ranges = trails->paths.paths;
    while (head < heads.size() || path < ranges.size()) {
        if (path == ranges.size() ||
            (head < heads.size() && heads[head].sample.birth < ranges[path].birth))
            ++head;
        else if (head == heads.size() || ranges[path].birth < heads[head].sample.birth)
            ++path;
        else {
            ++head;
            ++path;
        }
        ++count;
    }
    return count;
}
void validate_particle_world_paths(const ParticleEffectData& effect, double time,
                                   const ParticleWorldPaths& values,
                                   std::span<const ParticleWorldSample> heads) {
    validate_particle_effect(effect);
    validate_particle_world_paths(values);
    validate_particle_world_samples(effect, time, heads);
    require(particle_uses_scene_trails(effect) &&
            particle_world_capture_birth_count(heads, &values) <= effect.capacity);
    range(time, 0, 1e9);
    const auto window = particle_birth_window(effect, time, effect.trail->duration);
    for (const auto& path : values.paths.paths) {
        require(path.birth >= window.first && path.birth < window.last &&
                path.count <= effect.trail->segments + 1);
        const auto age = particle_birth_age(effect, window, time, path.birth);
        require(bool(age));
        const auto tolerance = 8 * std::numeric_limits<double>::epsilon() * std::max(1., *age);
        const auto tail = std::max(0., *age - effect.trail->duration);
        for (const auto& point : std::span(values.paths.points).subspan(path.first, path.count))
            require(point.age >= tail - tolerance &&
                    point.age <= std::min(*age, effect.lifetime) + tolerance);
    }
}
void validate_particle_world_samples(const ParticleEffectData& effect, double time,
                                     std::span<const ParticleWorldSample> samples) {
    validate_particle_effect(effect);
    validate_particle_world_samples(samples);
    if (!particle_uses_scene_collision(effect) || samples.size() > effect.capacity ||
        !std::isfinite(time) || time < 0 || time > 1e9)
        throw std::runtime_error(
            "Particle World capture does not match its effect or time profile");
    const auto window = particle_birth_window(effect, time);
    for (const auto& value : samples) {
        const auto ordinal = value.sample.birth;
        if (effect.emission == ParticleEmission::Burst && ordinal >= effect.burst_count)
            throw std::runtime_error("Particle World capture has a foreign burst ordinal");
        const auto age = particle_birth_age(effect, window, time, ordinal);
        if (!age || (effect.emission == ParticleEmission::Burst && !window.last) ||
            *age != value.sample.age)
            throw std::runtime_error("Particle World capture is outside its live emission phase");
    }
}
nlohmann::json encode_particle_world_paths(const ParticleWorldPaths& values) {
    validate_particle_world_paths(values);
    auto result =
        nlohmann::json{{"paths", nlohmann::json::array()}, {"points", nlohmann::json::array()}};
    for (std::size_t i = 0; i < values.paths.paths.size(); ++i) {
        const auto& path = values.paths.paths[i];
        result["paths"].push_back({path.birth, path.first, path.count, values.width_scales[i]});
    }
    for (const auto& point : values.paths.points)
        result["points"].push_back({point.age, point.position[0], point.position[1],
                                    point.position[2], point.size, point.color[0], point.color[1],
                                    point.color[2], point.opacity});
    return result;
}
ParticleWorldPaths decode_particle_world_paths(const nlohmann::json& source) {
    require(source.is_object() && source.size() == 2 && source.at("paths").is_array() &&
            source.at("points").is_array());
    const auto& paths = source.at("paths");
    const auto& points = source.at("points");
    require(paths.size() <= particle_scene_collision_capacity &&
            points.size() <= particle_scene_capacity + particle_scene_collision_capacity);
    ParticleWorldPaths result;
    result.paths.paths.reserve(paths.size());
    result.paths.points.reserve(points.size());
    result.width_scales.reserve(paths.size());
    for (const auto& row : paths) {
        require(row.is_array() && row.size() == 4 && row[0].is_number_unsigned() &&
                row[1].is_number_unsigned() && row[2].is_number_unsigned() &&
                row[1].get<std::uint64_t>() <= points.size() &&
                row[2].get<std::uint64_t>() <= points.size());
        result.paths.paths.push_back(
            {row[0].get<std::uint64_t>(), row[1].get<std::size_t>(), row[2].get<std::size_t>()});
        result.width_scales.push_back(number(row[3]));
    }
    for (const auto& row : points) {
        require(row.is_array() && row.size() == 9);
        ParticleSample point;
        point.age = number(row[0]);
        for (unsigned axis = 0; axis < 3; ++axis) {
            point.position[axis] = number(row[axis + 1]);
            const auto color = number(row[axis + 5]);
            range(color, 0, 1);
            point.color[axis] = float(color);
        }
        point.size = number(row[4]);
        point.opacity = number(row[8]);
        result.paths.points.push_back(point);
    }
    for (const auto& path : result.paths.paths) {
        require(path.first <= result.paths.points.size() &&
                path.count <= result.paths.points.size() - path.first);
        for (auto& point : std::span(result.paths.points).subspan(path.first, path.count))
            point.birth = path.birth;
    }
    validate_particle_world_paths(result);
    return result;
}
} // namespace forge
