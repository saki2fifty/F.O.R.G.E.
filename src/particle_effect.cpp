#include <algorithm>
#include <cmath>
#include <forge/particle_effect.hpp>
#include <numbers>
#include <random>
#include <set>
#include <stdexcept>
#include <type_traits>
namespace forge {
namespace {
void range(double value, double minimum, double maximum, const char* field) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error(std::string("Particle effect invalid ") + field);
}
double scalar(const nlohmann::json& value) {
    if (!value.is_number())
        throw std::runtime_error("Particle effect expects a number");
    return value.get<double>();
}
template <class T> void vector_value(const nlohmann::json& value, std::array<T, 3>& out) {
    if (!value.is_array() || value.size() != 3)
        throw std::runtime_error("Particle effect expects a three-value vector");
    for (unsigned i = 0; i < 3; ++i) {
        const auto number = scalar(value[i]);
        if constexpr (std::is_same_v<T, float>)
            range(number, 0, 1, "color");
        else
            range(number, -1e10, 1e10, "vector");
        out[i] = T(number);
    }
}
std::array<double, 3> cross(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
std::array<double, 3> normalized(std::array<double, 3> v) {
    const auto length = std::hypot(v[0], v[1], v[2]);
    for (auto& coordinate : v)
        coordinate /= length;
    return v;
}
} // namespace
void validate_particle_effect(const ParticleEffectData& data) {
    if (!data.mesh.id)
        throw std::runtime_error("Particle effect requires a Mesh");
    if (data.emission > ParticleEmission::Burst)
        throw std::runtime_error("Unknown particle emission mode");
    range(data.capacity, 1, 1024, "capacity");
    range(data.burst_count, 1, 1024, "burst count");
    if (data.emission == ParticleEmission::Burst && data.burst_count > data.capacity)
        throw std::runtime_error("Particle burst count exceeds effect capacity");
    range(data.spawn_rate, .01, 256, "spawn rate");
    range(data.lifetime, .01, 30, "lifetime");
    if (data.emission == ParticleEmission::Continuous &&
        std::ceil(data.spawn_rate * data.lifetime) > data.capacity)
        throw std::runtime_error("Particle rate and lifetime exceed effect capacity");
    for (auto coordinate : data.direction)
        range(coordinate, -1e6, 1e6, "direction");
    if (std::hypot(data.direction[0], data.direction[1], data.direction[2]) < 1e-12)
        throw std::runtime_error("Particle direction must be nonzero");
    for (auto value : data.acceleration)
        range(value, -10000, 10000, "acceleration");
    range(data.cone_angle, 0, 180, "cone angle");
    range(data.speed_min, 0, 1000, "minimum speed");
    range(data.speed_max, data.speed_min, 1000, "maximum speed");
    range(data.size_start, .001, 100, "start size");
    range(data.size_end, .001, 100, "end size");
    for (auto value : data.color_start)
        range(value, 0, 1, "start color");
    for (auto value : data.color_end)
        range(value, 0, 1, "end color");
}
ParticleEffectData decode_particle_effect(const nlohmann::json& source) {
    if (!source.is_object() || source.at("format") != "forge.particle_effect" ||
        !source.at("version").is_number_integer() || source.at("version") != 1 ||
        source.at("profile") != "cpu_mesh1")
        throw std::runtime_error("Unsupported particle effect format/version/profile");
    static const std::set<std::string> fields{
        "format",     "version",      "profile",     "mesh",       "material",
        "emission",   "capacity",     "burst_count", "spawn_rate", "lifetime",
        "direction",  "acceleration", "cone_angle",  "speed_min",  "speed_max",
        "size_start", "size_end",     "color_start", "color_end",  "extensions"};
    for (const auto& [name, value] : source.items())
        if (!fields.contains(name))
            throw std::runtime_error("Unknown particle effect field: " + name);
    if (source.contains("extensions") && !source.at("extensions").is_object())
        throw std::runtime_error("Particle effect extensions must be an object");
    ParticleEffectData data;
    data.mesh = source.at("mesh").get<AssetRef<MeshAsset>>();
    if (source.contains("material"))
        data.material = source.at("material").get<AssetRef<MaterialAsset>>();
    if (source.contains("emission")) {
        const auto& mode = source.at("emission");
        if (mode == "continuous")
            data.emission = ParticleEmission::Continuous;
        else if (mode == "burst")
            data.emission = ParticleEmission::Burst;
        else
            throw std::runtime_error("Unknown particle emission mode");
    }
    for (const auto name : {"capacity", "burst_count"})
        if (source.contains(name)) {
            const auto& value = source.at(name);
            if (!value.is_number_integer())
                throw std::runtime_error("Particle count must be an integer");
            const auto number = scalar(value);
            range(number, 1, 1024, name);
            (std::string_view(name) == "capacity" ? data.capacity : data.burst_count) =
                std::uint32_t(number);
        }
    auto number = [&](const char* name, double& value) {
        if (source.contains(name))
            value = scalar(source.at(name));
    };
    number("spawn_rate", data.spawn_rate);
    number("lifetime", data.lifetime);
    number("cone_angle", data.cone_angle);
    number("speed_min", data.speed_min);
    number("speed_max", data.speed_max);
    number("size_start", data.size_start);
    number("size_end", data.size_end);
    if (source.contains("direction"))
        vector_value(source.at("direction"), data.direction);
    if (source.contains("acceleration"))
        vector_value(source.at("acceleration"), data.acceleration);
    if (source.contains("color_start"))
        vector_value(source.at("color_start"), data.color_start);
    if (source.contains("color_end"))
        vector_value(source.at("color_end"), data.color_end);
    validate_particle_effect(data);
    return data;
}
nlohmann::json encode_particle_effect(const ParticleEffectData& data) {
    validate_particle_effect(data);
    nlohmann::json result{
        {"format", "forge.particle_effect"},
        {"version", 1},
        {"profile", "cpu_mesh1"},
        {"mesh", data.mesh},
        {"emission", data.emission == ParticleEmission::Burst ? "burst" : "continuous"},
        {"capacity", data.capacity},
        {"burst_count", data.burst_count},
        {"spawn_rate", data.spawn_rate},
        {"lifetime", data.lifetime},
        {"direction", data.direction},
        {"acceleration", data.acceleration},
        {"cone_angle", data.cone_angle},
        {"speed_min", data.speed_min},
        {"speed_max", data.speed_max},
        {"size_start", data.size_start},
        {"size_end", data.size_end},
        {"color_start", data.color_start},
        {"color_end", data.color_end}};
    if (data.material.id)
        result["material"] = data.material;
    return result;
}
std::vector<ParticleSample> sample_particle_effect(const ParticleEffectData& data, double seconds,
                                                   std::uint32_t seed, std::size_t budget) {
    validate_particle_effect(data);
    range(seconds, 0, 1e9, "sample time");
    const double phase = seconds * data.spawn_rate;
    const double window = data.lifetime * data.spawn_rate;
    std::uint64_t first = 0, last = 0;
    if (data.emission == ParticleEmission::Burst) {
        if (seconds >= data.lifetime)
            return {};
        last = data.burst_count;
    } else {
        last = std::uint64_t(std::floor(phase));
        if (phase >= window) {
            first = std::uint64_t(std::floor(phase - window));
            // Subtracting a short window from a large phase can round across
            // one birth. Nominate it too, then apply the exact phase predicate.
            if (first)
                --first;
        }
    }
    // Rounded products can nominate one extra boundary birth. Check exact age
    // before the final budget decision; never drop a valid particle to fit.
    if (last - first > std::uint64_t(data.capacity) + 1)
        throw std::runtime_error("Particle sample exceeds instance budget");
    std::vector<ParticleSample> result;
    result.reserve(std::size_t(last - first));
    const auto axis = normalized(data.direction);
    const auto side = normalized(
        cross(axis, std::abs(axis[1]) > .9 ? std::array{1., 0., 0.} : std::array{0., 1., 0.}));
    const auto up = cross(axis, side);
    const auto cosine = std::cos(data.cone_angle * std::numbers::pi / 180);
    for (auto ordinal = first; ordinal < last; ++ordinal) {
        ParticleSample particle;
        particle.birth = ordinal;
        if (data.emission == ParticleEmission::Burst)
            particle.age = seconds;
        else {
            const auto phase_age = phase - double(ordinal + 1);
            if (phase_age < 0 || phase_age >= window)
                continue;
            // Valid phase age can round to the exact endpoint when converted
            // back to seconds; retain the representable live-side value.
            particle.age = std::min(phase_age / data.spawn_rate, std::nextafter(data.lifetime, 0.));
        }
        std::seed_seq sequence{seed, std::uint32_t(ordinal), std::uint32_t(ordinal >> 32)};
        std::minstd_rand random(sequence);
        auto unit = [&] {
            return double(random() - random.min()) / (double(random.max()) - random.min() + 1);
        };
        const auto c = 1 - unit() * (1 - cosine), s = std::sqrt(std::max(0., 1 - c * c)),
                   phi = unit() * 2 * std::numbers::pi;
        const auto speed = data.speed_min + unit() * (data.speed_max - data.speed_min);
        const auto fraction = particle.age / data.lifetime;
        particle.size = std::lerp(data.size_start, data.size_end, fraction);
        for (unsigned i = 0; i < 3; ++i) {
            const auto initial =
                speed * (axis[i] * c + side[i] * s * std::cos(phi) + up[i] * s * std::sin(phi));
            particle.position[i] =
                initial * particle.age + .5 * data.acceleration[i] * particle.age * particle.age;
            particle.velocity[i] = initial + data.acceleration[i] * particle.age;
            particle.color[i] =
                float(std::lerp(double(data.color_start[i]), double(data.color_end[i]), fraction));
        }
        if (result.size() >= data.capacity || result.size() >= budget)
            throw std::runtime_error("Particle sample exceeds instance budget");
        result.push_back(particle);
    }
    return result;
}
} // namespace forge
