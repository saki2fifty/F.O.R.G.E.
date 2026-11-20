#include "particle_motion.hpp"
#include "particle_path.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
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
void validate_trail_parameters(const ParticleEffectData& data, double duration, unsigned segments) {
    validate_particle_trail_settings({duration, segments});
    if (data.capacity > particle_scene_capacity / segments)
        throw std::runtime_error("Particle paths exceed the bounded segment capacity");
    if (data.emission == ParticleEmission::Continuous &&
        std::ceil(data.spawn_rate * (data.lifetime + duration)) > data.capacity)
        throw std::runtime_error("Particle rate, lifetime and trail duration exceed capacity");
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
template <class Stops> void validate_stops(const Stops& stops, bool gpu) {
    if (stops.empty())
        return;
    if (stops.size() < 2 || stops.size() > 16 || stops.front().time != 0 || stops.back().time != 1)
        throw std::runtime_error("Particle lifetime values need 2–16 stops and endpoints 0/1");
    double previous = -1;
    float previous_gpu = -1;
    for (const auto& stop : stops) {
        range(stop.time, 0, 1, "stop time");
        if (stop.time <= previous)
            throw std::runtime_error("Particle stop times must be distinct and ordered");
        previous = stop.time;
        if (gpu) {
            const auto time = float(stop.time);
            if (time <= previous_gpu)
                throw std::runtime_error(
                    "GPU particle stop times must remain distinct at float precision");
            previous_gpu = time;
        }
        if constexpr (std::is_arithmetic_v<decltype(stop.value)>) {
            if constexpr (std::is_same_v<Stops, std::vector<ParticleSizeStop>>)
                range(stop.value, .001, 100, "size stop");
            else
                range(stop.value, 0, 1, "opacity stop");
        } else
            for (const auto value : stop.value)
                range(value, 0, 1, "color stop");
    }
}
template <class Stops> auto sample_stops(const Stops& stops, double time) {
    const auto next = std::upper_bound(stops.begin(), stops.end(), time,
                                       [](double at, const auto& stop) { return at < stop.time; });
    if (next == stops.begin())
        return next->value;
    if (next == stops.end())
        return stops.back().value;
    const auto& previous = *(next - 1);
    if (previous.hold)
        return previous.value;
    const auto fraction = (time - previous.time) / (next->time - previous.time);
    if constexpr (std::is_arithmetic_v<decltype(previous.value)>)
        return std::lerp(previous.value, next->value, fraction);
    else {
        auto value = previous.value;
        for (unsigned axis = 0; axis < 3; ++axis)
            value[axis] =
                float(std::lerp(double(value[axis]), double(next->value[axis]), fraction));
        return value;
    }
}
struct ParticleSpawnFrame {
    std::array<double, 3> axis, side, up;
    double cosine;
};
ParticleSpawnFrame particle_spawn_frame(const ParticleEffectData& data) {
    const auto axis = normalized(data.direction);
    const auto side = normalized(
        cross(axis, std::abs(axis[1]) > .9 ? std::array{1., 0., 0.} : std::array{0., 1., 0.}));
    return {axis, side, cross(axis, side), std::cos(data.cone_angle * std::numbers::pi / 180)};
}
struct ParticleBirthState {
    std::uint64_t ordinal;
    std::array<double, 3> origin{}, velocity{};
};
ParticleBirthState particle_birth_state(const ParticleEffectData& data,
                                        const ParticleSpawnFrame& frame, std::uint32_t seed,
                                        std::uint64_t ordinal) {
    ParticleBirthState state{ordinal};
    std::seed_seq sequence{seed, std::uint32_t(ordinal), std::uint32_t(ordinal >> 32)};
    std::minstd_rand random(sequence);
    auto unit = [&] {
        return double(random() - random.min()) / (double(random.max()) - random.min() + 1);
    };
    const auto c = 1 - unit() * (1 - frame.cosine), s = std::sqrt(std::max(0., 1 - c * c)),
               phi = unit() * 2 * std::numbers::pi;
    const auto speed = data.speed_min + unit() * (data.speed_max - data.speed_min);
    if (data.shape == ParticleSpawnShape::Box) {
        for (unsigned i = 0; i < 3; ++i)
            state.origin[i] = (unit() - .5) * data.shape_size[i];
    } else if (data.shape == ParticleSpawnShape::Sphere) {
        const auto radius = data.shape_radius * std::cbrt(unit()), y = 2 * unit() - 1,
                   radial = std::sqrt(std::max(0., 1 - y * y)),
                   angle = unit() * 2 * std::numbers::pi;
        state.origin = {radius * radial * std::cos(angle), radius * y,
                        radius * radial * std::sin(angle)};
    } else if (data.shape == ParticleSpawnShape::Disc) {
        const auto radius = data.shape_radius * std::sqrt(unit()),
                   angle = unit() * 2 * std::numbers::pi;
        for (unsigned i = 0; i < 3; ++i)
            state.origin[i] =
                radius * (frame.side[i] * std::cos(angle) + frame.up[i] * std::sin(angle));
    }
    for (unsigned i = 0; i < 3; ++i)
        state.velocity[i] = speed * (frame.axis[i] * c + frame.side[i] * s * std::cos(phi) +
                                     frame.up[i] * s * std::sin(phi));
    return state;
}
ParticleSample sample_particle_birth(const ParticleEffectData& data,
                                     const ParticleBirthState& state, double age,
                                     const ParticleMotionState* collision_motion = nullptr) {
    ParticleSample particle;
    particle.birth = state.ordinal;
    particle.age = age;
    const auto fraction = particle.age / data.lifetime;
    if (particle_uses_opacity(data))
        particle.opacity = data.opacity_curve.empty()
                               ? std::lerp(data.opacity_start, data.opacity_end, fraction)
                               : sample_stops(data.opacity_curve, fraction);
    particle.size = data.size_curve.empty() ? std::lerp(data.size_start, data.size_end, fraction)
                                            : sample_stops(data.size_curve, fraction);
    if (!data.color_gradient.empty())
        particle.color = sample_stops(data.color_gradient, fraction);
    const auto motion = collision_motion
                            ? *collision_motion
                            : evaluate_particle_motion({state.origin, state.velocity},
                                                       data.acceleration, data.drag, particle.age);
    particle.position = motion.position;
    particle.velocity = motion.velocity;
    for (unsigned i = 0; i < 3; ++i) {
        if (data.color_gradient.empty())
            particle.color[i] =
                float(std::lerp(double(data.color_start[i]), double(data.color_end[i]), fraction));
    }
    return particle;
}
struct ParticleBirthWindow {
    double phase, units, lifetime;
    std::uint64_t first = 0, last = 0;
};
ParticleBirthWindow particle_birth_window(const ParticleEffectData& data, double seconds,
                                          double tail = 0) {
    ParticleBirthWindow result{seconds * data.spawn_rate, (data.lifetime + tail) * data.spawn_rate,
                               data.lifetime + tail};
    if (data.emission == ParticleEmission::Burst) {
        if (seconds < result.lifetime)
            result.last = data.burst_count;
    } else {
        result.last = std::uint64_t(std::floor(result.phase));
        if (result.phase >= result.units) {
            result.first = std::uint64_t(std::floor(result.phase - result.units));
            // Nominate the boundary birth before applying exact phase membership.
            if (result.first)
                --result.first;
        }
    }
    if (result.last - result.first > std::uint64_t(data.capacity) + 1)
        throw std::runtime_error("Particle sample exceeds instance budget");
    return result;
}
std::optional<double> particle_birth_age(const ParticleEffectData& data,
                                         const ParticleBirthWindow& window, double seconds,
                                         std::uint64_t ordinal) {
    if (data.emission == ParticleEmission::Burst)
        return seconds;
    const auto phase_age = window.phase - double(ordinal + 1);
    if (phase_age < 0 || phase_age >= window.units)
        return std::nullopt;
    // Preserve a live phase that rounds to the exact lifetime in seconds.
    return std::min(phase_age / data.spawn_rate, std::nextafter(window.lifetime, 0.));
}
bool stop_hold(const nlohmann::json& stop) {
    if (!stop.is_object() || stop.size() != 3 || !stop.contains("time") ||
        !stop.contains("value") || !stop.contains("interpolation"))
        throw std::runtime_error("Invalid particle lifetime stop fields");
    const auto& mode = stop.at("interpolation");
    if (mode != "linear" && mode != "step")
        throw std::runtime_error("Unknown particle stop interpolation");
    return mode == "step";
}
} // namespace
void validate_particle_graph_bounds(const std::array<double, 3>& extent, double maximum_size) {
    const auto finite_bound = [](double value, double maximum) {
        range(value, 0, maximum, "graph bounds");
        if (value != 0 && float(value) == 0)
            throw std::runtime_error("Particle graph bound is below finite GPU representation");
    };
    for (double value : extent)
        finite_bound(value, 1000000);
    finite_bound(maximum_size, 10000);
}
void validate_particle_graph_program(const ParticleGraphProgram& program) {
    if (!program.shader.id)
        throw std::runtime_error("Particle graph requires a Shader asset");
    validate_particle_graph_bounds(program.extent, program.maximum_size);
}
ParticleGraphProgram decode_particle_graph_program(const nlohmann::json& source) {
    if (!source.is_object() || source.size() != 3)
        throw std::runtime_error("Particle graph program requires Shader, extent and maximum size");
    ParticleGraphProgram result;
    result.shader = source.at("shader").get<AssetRef<ShaderAsset>>();
    vector_value(source.at("extent"), result.extent);
    result.maximum_size = scalar(source.at("maximum_size"));
    validate_particle_graph_program(result);
    return result;
}
nlohmann::json encode_particle_graph_program(const ParticleGraphProgram& program) {
    validate_particle_graph_program(program);
    return {{"shader", program.shader},
            {"extent", program.extent},
            {"maximum_size", program.maximum_size}};
}
void validate_particle_trail_settings(const ParticleTrailSettings& trail) {
    range(trail.duration, particle_path_min_duration, particle_path_max_duration,
          "trail duration [0.001,30]");
    if (!trail.segments || trail.segments > particle_path_segment_limit)
        throw std::runtime_error("Particle trails require 1–16 segments");
}
ParticleTrailSettings decode_particle_trail_settings(const nlohmann::json& source) {
    if (!source.is_object() || source.size() != 2 || !source.at("segments").is_number_integer())
        throw std::runtime_error("Particle trail requires duration and integer segments");
    const auto segments = scalar(source.at("segments"));
    range(segments, 1, particle_path_segment_limit, "trail segments [1,16]");
    ParticleTrailSettings result{scalar(source.at("duration")), unsigned(segments)};
    validate_particle_trail_settings(result);
    return result;
}
nlohmann::json encode_particle_trail_settings(const ParticleTrailSettings& trail) {
    validate_particle_trail_settings(trail);
    return {{"duration", trail.duration}, {"segments", trail.segments}};
}
ParticleCollisionPlane decode_particle_collision_plane(const nlohmann::json& source) {
    if (!source.is_object() || source.size() != 6 || !source.at("bounce_limit").is_number_integer())
        throw std::runtime_error(
            "Particle collision plane requires six fields and integer bounce limit");
    ParticleCollisionPlane plane;
    vector_value(source.at("normal"), plane.normal);
    plane.offset = scalar(source.at("offset"));
    plane.radius = scalar(source.at("radius"));
    plane.restitution = scalar(source.at("restitution"));
    plane.friction = scalar(source.at("friction"));
    const auto bounces = scalar(source.at("bounce_limit"));
    range(bounces, 0, 16, "plane bounce limit [0,16]");
    plane.bounce_limit = unsigned(bounces);
    validate_particle_collision_plane(plane);
    return plane;
}
nlohmann::json encode_particle_collision_plane(const ParticleCollisionPlane& plane) {
    validate_particle_collision_plane(plane);
    return {{"normal", plane.normal},     {"offset", plane.offset},
            {"radius", plane.radius},     {"restitution", plane.restitution},
            {"friction", plane.friction}, {"bounce_limit", plane.bounce_limit}};
}
void validate_particle_effect(const ParticleEffectData& data) {
    if (data.version < 1 || data.version > 11)
        throw std::runtime_error("Unknown particle value version");
    if (!data.mesh.id)
        throw std::runtime_error("Particle effect requires a Mesh");
    if (particle_uses_billboards(data) && data.mesh != engine_primitive(8))
        throw std::runtime_error("Camera-aligned particles require Engine/Quad geometry");
    if (particle_uses_trails(data) != bool(data.trail))
        throw std::runtime_error("Particle trail settings require cpu_trail1 values");
    if (data.trail) {
        if (data.mesh != engine_primitive(8))
            throw std::runtime_error("Particle trails require Engine/Quad geometry");
        validate_trail_parameters(data, data.trail->duration, data.trail->segments);
    }
    if (particle_uses_collision(data) != bool(data.collision))
        throw std::runtime_error("Particle plane collision settings require cpu_collision1 values");
    if (data.collision)
        validate_particle_collision_plane(*data.collision);
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
    if (data.shape > ParticleSpawnShape::Disc)
        throw std::runtime_error("Unknown particle origin shape");
    for (auto size : data.shape_size)
        range(size, .001, 1000, "shape dimensions");
    range(data.shape_radius, .001, 1000, "shape radius");
    range(data.drag, 0, 64, "drag");
    validate_stops(data.size_curve, particle_uses_gpu(data));
    validate_stops(data.color_gradient, particle_uses_gpu(data));
    range(data.opacity_start, 0, 1, "start opacity");
    range(data.opacity_end, 0, 1, "end opacity");
    validate_stops(data.opacity_curve, false);
    if (!particle_uses_opacity(data) &&
        (data.opacity_start != 1 || data.opacity_end != 1 || !data.opacity_curve.empty()))
        throw std::runtime_error("Particle opacity requires the CPU opacity profile");
    if (particle_uses_graph(data)) {
        if (!data.program)
            throw std::runtime_error("Particle graph requires a program");
        validate_particle_graph_program(*data.program);
    } else if (data.program)
        throw std::runtime_error("Particle graph values require the graph GPU profile");
    if (data.version == 1 &&
        (data.shape != ParticleSpawnShape::Point || data.shape_size != std::array{1., 1., 1.} ||
         data.shape_radius != 1 || data.drag != 0 || !data.size_curve.empty() ||
         !data.color_gradient.empty()))
        throw std::runtime_error("Extended particle values require cpu_mesh2");
}
ParticleEffectData decode_particle_effect(const nlohmann::json& source) {
    if (!source.is_object() || source.at("format") != "forge.particle_effect" ||
        !source.at("version").is_number_integer())
        throw std::runtime_error("Unsupported particle effect format/version/profile");
    const auto version = source.at("version").get<std::uint64_t>();
    const auto profile = source.at("profile").get<std::string>();
    if (!((version == 1 && profile == "cpu_mesh1") || (version == 2 && profile == "cpu_mesh2") ||
          (version == 3 && profile == "gpu_mesh1") || (version == 4 && profile == "gpu_graph1") ||
          (version == 5 && profile == "cpu_alpha1") || (version == 6 && profile == "cpu_alpha2") ||
          (version == 7 && profile == "cpu_billboard1") ||
          (version == 8 && profile == "gpu_billboard1") ||
          (version == 9 && profile == "gpu_graph_billboard1") ||
          (version == 10 && profile == "cpu_trail1") ||
          (version == 11 && profile == "cpu_collision1")))
        throw std::runtime_error("Unsupported particle effect format/version/profile");
    static const std::set<std::string> fields{
        "format",     "version",      "profile",     "mesh",       "material",
        "emission",   "capacity",     "burst_count", "spawn_rate", "lifetime",
        "direction",  "acceleration", "cone_angle",  "speed_min",  "speed_max",
        "size_start", "size_end",     "color_start", "color_end",  "extensions"};
    for (const auto& [name, value] : source.items())
        if (!fields.contains(name) &&
            !(version >= 2 &&
              (name == "shape" || name == "shape_size" || name == "shape_radius" ||
               name == "drag" || name == "size_curve" || name == "color_gradient")) &&
            !((version == 4 || version == 9) && name == "program") &&
            !(version == 10 && name == "trail") && !(version == 11 && name == "collision") &&
            !((version == 6 || version == 7 || version == 10 || version == 11) &&
              (name == "opacity_start" || name == "opacity_end" || name == "opacity_curve")))
            throw std::runtime_error("Unknown particle effect field: " + name);
    if (source.contains("extensions") && !source.at("extensions").is_object())
        throw std::runtime_error("Particle effect extensions must be an object");
    ParticleEffectData data;
    data.version = std::uint32_t(version);
    if (particle_uses_trails(data))
        data.trail = decode_particle_trail_settings(source.at("trail"));
    if (particle_uses_collision(data))
        data.collision = decode_particle_collision_plane(source.at("collision"));
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
    if (particle_uses_opacity(data)) {
        data.opacity_start = scalar(source.at("opacity_start"));
        data.opacity_end = scalar(source.at("opacity_end"));
        const auto& stops = source.at("opacity_curve");
        if (!stops.is_array() || stops.size() > 16)
            throw std::runtime_error("Particle opacity stop count exceeds limit");
        for (const auto& stop : stops)
            data.opacity_curve.push_back(
                {scalar(stop.at("time")), scalar(stop.at("value")), stop_hold(stop)});
    }
    if (source.contains("direction"))
        vector_value(source.at("direction"), data.direction);
    if (source.contains("acceleration"))
        vector_value(source.at("acceleration"), data.acceleration);
    if (source.contains("color_start"))
        vector_value(source.at("color_start"), data.color_start);
    if (source.contains("color_end"))
        vector_value(source.at("color_end"), data.color_end);
    if (data.version >= 2) {
        if (source.contains("shape")) {
            const auto& shape = source.at("shape");
            if (shape == "point")
                data.shape = ParticleSpawnShape::Point;
            else if (shape == "box")
                data.shape = ParticleSpawnShape::Box;
            else if (shape == "sphere")
                data.shape = ParticleSpawnShape::Sphere;
            else if (shape == "disc")
                data.shape = ParticleSpawnShape::Disc;
            else
                throw std::runtime_error("Unknown particle origin shape");
        }
        if (source.contains("shape_size"))
            vector_value(source.at("shape_size"), data.shape_size);
        if (source.contains("shape_radius"))
            data.shape_radius = scalar(source.at("shape_radius"));
        if (source.contains("drag"))
            data.drag = scalar(source.at("drag"));
        for (const auto* field : {"size_curve", "color_gradient"})
            if (source.contains(field)) {
                const auto& stops = source.at(field);
                if (!stops.is_array() || stops.size() > 16)
                    throw std::runtime_error("Particle stop count exceeds limit");
                for (const auto& stop : stops) {
                    const auto hold = stop_hold(stop);
                    if (std::string_view(field) == "size_curve")
                        data.size_curve.push_back(
                            {scalar(stop.at("time")), scalar(stop.at("value")), hold});
                    else {
                        ParticleColorStop value;
                        value.time = scalar(stop.at("time"));
                        value.hold = hold;
                        vector_value(stop.at("value"), value.value);
                        data.color_gradient.push_back(value);
                    }
                }
            }
    }
    if (particle_uses_graph(data))
        data.program = decode_particle_graph_program(source.at("program"));
    validate_particle_effect(data);
    return data;
}
nlohmann::json encode_particle_effect(const ParticleEffectData& data) {
    validate_particle_effect(data);
    nlohmann::json result{
        {"format", "forge.particle_effect"},
        {"version", data.version},
        {"profile", data.version == 1    ? "cpu_mesh1"
                    : data.version == 2  ? "cpu_mesh2"
                    : data.version == 3  ? "gpu_mesh1"
                    : data.version == 4  ? "gpu_graph1"
                    : data.version == 5  ? "cpu_alpha1"
                    : data.version == 6  ? "cpu_alpha2"
                    : data.version == 7  ? "cpu_billboard1"
                    : data.version == 8  ? "gpu_billboard1"
                    : data.version == 9  ? "gpu_graph_billboard1"
                    : data.version == 10 ? "cpu_trail1"
                                         : "cpu_collision1"},
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
    if (data.version >= 2) {
        constexpr const char* shapes[]{"point", "box", "sphere", "disc"};
        result["shape"] = shapes[unsigned(data.shape)];
        result["shape_size"] = data.shape_size;
        result["shape_radius"] = data.shape_radius;
        result["drag"] = data.drag;
        result["size_curve"] = nlohmann::json::array();
        result["color_gradient"] = nlohmann::json::array();
        for (const auto& stop : data.size_curve)
            result["size_curve"].push_back({{"time", stop.time},
                                            {"value", stop.value},
                                            {"interpolation", stop.hold ? "step" : "linear"}});
        for (const auto& stop : data.color_gradient)
            result["color_gradient"].push_back({{"time", stop.time},
                                                {"value", stop.value},
                                                {"interpolation", stop.hold ? "step" : "linear"}});
    }
    if (data.trail)
        result["trail"] = encode_particle_trail_settings(*data.trail);
    if (data.collision)
        result["collision"] = encode_particle_collision_plane(*data.collision);
    if (data.program)
        result["program"] = encode_particle_graph_program(*data.program);
    if (particle_uses_opacity(data)) {
        result["opacity_start"] = data.opacity_start;
        result["opacity_end"] = data.opacity_end;
        result["opacity_curve"] = nlohmann::json::array();
        for (const auto& stop : data.opacity_curve)
            result["opacity_curve"].push_back({{"time", stop.time},
                                               {"value", stop.value},
                                               {"interpolation", stop.hold ? "step" : "linear"}});
    }
    return result;
}
std::vector<ParticleSample> sample_particle_effect(const ParticleEffectData& data, double seconds,
                                                   std::uint32_t seed, std::size_t budget) {
    validate_particle_effect(data);
    if (particle_uses_gpu(data))
        throw std::runtime_error("GPU particle samples require a rendering device");
    range(seconds, 0, 1e9, "sample time");
    const auto window = particle_birth_window(data, seconds);
    if (data.emission == ParticleEmission::Burst && !window.last)
        return {};
    std::vector<ParticleSample> result;
    result.reserve(std::size_t(window.last - window.first));
    const auto frame = particle_spawn_frame(data);
    const auto plane = data.collision
                           ? std::optional{prepare_particle_collision_plane(*data.collision)}
                           : std::nullopt;
    for (auto ordinal = window.first; ordinal < window.last; ++ordinal) {
        const auto age = particle_birth_age(data, window, seconds, ordinal);
        if (!age)
            continue;
        const auto state = particle_birth_state(data, frame, seed, ordinal);
        std::optional<ParticleMotionState> motion;
        if (plane) {
            const auto contact = evaluate_particle_plane_motion(
                {state.origin, state.velocity}, data.acceleration, data.drag, *age, *plane);
            if (contact.death_age)
                continue;
            motion = contact.state;
        }
        const auto particle = sample_particle_birth(data, state, *age, motion ? &*motion : nullptr);
        if (result.size() >= data.capacity || result.size() >= budget)
            throw std::runtime_error("Particle sample exceeds instance budget");
        result.push_back(particle);
    }
    return result;
}
ParticlePathSamples sample_particle_paths(const ParticleEffectData& data, double seconds,
                                          std::uint32_t seed, double duration, unsigned segments,
                                          std::size_t budget) {
    validate_particle_effect(data);
    if (particle_uses_gpu(data) || particle_uses_collision(data))
        throw std::runtime_error("Particle paths require CPU evaluation without plane collision");
    range(seconds, 0, 1e9, "sample time");
    validate_trail_parameters(data, duration, segments);
    const auto window = particle_birth_window(data, seconds, duration);
    ParticlePathSamples result;
    const auto heads = std::min<std::size_t>(data.capacity, window.last - window.first);
    result.paths.reserve(heads);
    result.points.reserve(std::min(budget, std::size_t(data.capacity) * segments) + heads);
    const auto frame = particle_spawn_frame(data);
    for (auto ordinal = window.first; ordinal < window.last; ++ordinal) {
        const auto age = particle_birth_age(data, window, seconds, ordinal);
        if (!age)
            continue;
        std::array<double, particle_path_segment_limit + 1> ages{};
        std::size_t count = 0;
        for (unsigned point = 0; point <= segments; ++point) {
            const auto at =
                std::clamp(*age - duration * (segments - point) / segments, 0., data.lifetime);
            if (!count || at > ages[count - 1])
                ages[count++] = at;
        }
        if (count < 2)
            continue;
        if (result.paths.size() >= data.capacity || count - 1 > budget - result.segment_count())
            throw std::runtime_error("Particle path sample exceeds segment budget");
        const auto state = particle_birth_state(data, frame, seed, ordinal);
        const auto first = result.points.size();
        for (std::size_t point = 0; point < count; ++point)
            result.points.push_back(sample_particle_birth(data, state, ages[point]));
        result.paths.push_back({ordinal, first, count});
    }
    return result;
}
std::size_t particle_instance_capacity(const ParticleEffectData& data) {
    validate_particle_effect(data);
    return std::size_t(data.capacity) * (data.trail ? data.trail->segments : 1u);
}
void validate_particle_instance_parameters(double size, std::array<float, 3> tint) {
    range(size, .01, 16, "emitter size multiplier [0.01,16]");
    for (const auto channel : tint)
        range(channel, 0, 1, "emitter linear RGB tint [0,1]");
}
void modulate_particle_samples(std::span<ParticleSample> samples, double size,
                               std::array<float, 3> tint) {
    // Reject the whole parameter set before touching caller-owned samples.
    validate_particle_instance_parameters(size, tint);
    for (auto& sample : samples) {
        sample.size *= size;
        for (unsigned channel = 0; channel < 3; ++channel)
            sample.color[channel] *= tint[channel];
    }
}
} // namespace forge
