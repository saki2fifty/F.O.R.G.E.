#include "particle_gpu_plan.hpp"
#include "particle_motion.hpp"
#include "particle_render_instances.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace forge {
namespace {
float gpu(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::runtime_error("Particle compute input exceeds finite GPU values");
    return float(value);
}
std::array<double, 3> cross(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
std::array<double, 3> unit(std::array<double, 3> value) {
    const auto length = std::hypot(value[0], value[1], value[2]);
    for (auto& coordinate : value)
        coordinate /= length;
    return value;
}
template <class Stops> void gpu_times(const Stops& stops) {
    float previous = -1;
    for (const auto& stop : stops) {
        const auto time = gpu(stop.time);
        if (time <= previous)
            throw std::runtime_error(
                "GPU particle stop times must remain distinct at float precision");
        previous = time;
    }
}
} // namespace
void validate_particle_gpu_collision_values(const ParticleGpuCollisionValues& values) {
    const auto& n = values.plane;
    const auto& response = values.response;
    const double length = std::hypot(double(n[0]), double(n[1]), double(n[2]));
    if (!std::isfinite(length) || std::abs(length - 1) > 2e-6 || !std::isfinite(n[3]) ||
        std::abs(n[3]) > 1001000 || !std::isfinite(response[0]) || response[0] < 0 ||
        response[0] > 1 || !std::isfinite(response[1]) || response[1] < 0 || response[1] > 1 ||
        !std::isfinite(response[2]) || response[2] < 0 || response[2] > 16 ||
        std::floor(response[2]) != response[2] || response[3] != 0)
        throw std::runtime_error("GPU particle plane response exceeds its bounded motion profile");
}
ParticleGpuCollisionValues
prepare_particle_gpu_collision_values(const ParticleCollisionPlane& source) {
    const auto plane = prepare_particle_collision_plane(source);
    ParticleGpuCollisionValues result;
    for (unsigned i = 0; i < 3; ++i)
        result.plane[i] = gpu(plane.normal[i]);
    result.plane[3] = gpu(plane.offset + plane.radius);
    result.response = {gpu(plane.restitution), gpu(plane.friction), float(plane.bounce_limit), 0};
    validate_particle_gpu_collision_values(result);
    return result;
}
void validate_particle_gpu_opacity_values(const ParticleGpuOpacityValues& values) {
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    if (values.count > values.stops.size() || values.count == 1 || !unit(values.endpoints[0]) ||
        !unit(values.endpoints[1]))
        throw std::runtime_error("GPU particle opacity exceeds its bounded profile");
    float previous = -1;
    for (unsigned i = 0; i < values.count; ++i) {
        const auto& stop = values.stops[i];
        if (!unit(stop[0]) || !unit(stop[1]) || stop[0] <= previous ||
            (stop[2] != 0 && stop[2] != 1) || stop[3] != 0 || (i == 0 && stop[0] != 0) ||
            (i + 1 == values.count && stop[0] != 1))
            throw std::runtime_error("GPU particle opacity stops require distinct normalized "
                                     "times, endpoints and Step/Linear values");
        previous = stop[0];
    }
}
ParticleGpuOpacityValues prepare_particle_gpu_opacity_values(const ParticleEffectData& data) {
    validate_particle_effect(data);
    if (!particle_uses_gpu_opacity(data))
        throw std::runtime_error("GPU opacity packing requires an explicit GPU opacity profile");
    ParticleGpuOpacityValues result;
    result.endpoints = {gpu(data.opacity_start), gpu(data.opacity_end)};
    result.count = unsigned(data.opacity_curve.size());
    for (unsigned i = 0; i < result.count; ++i) {
        const auto& stop = data.opacity_curve[i];
        result.stops[i] = {gpu(stop.time), gpu(stop.value), stop.hold ? 1.f : 0.f, 0};
    }
    validate_particle_gpu_opacity_values(result);
    return result;
}
ParticleGpuBillboardValues prepare_particle_gpu_billboard_values(const CameraView& camera,
                                                                 unsigned count) {
    if (count > 1024)
        throw std::runtime_error("GPU particle camera projection exceeds its capacity");
    const auto frame = particle_billboard_world(AffineTransform{}, camera);
    ParticleGpuBillboardValues result;
    result.counts[0] = count;
    result.counts[1] = 1;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column)
            result.camera[row][column] = gpu(frame.m[4 * row + column]);
    return result;
}
void validate_particle_gpu_graph_limits(const ParticleGpuGraphLimits& limits) {
    validate_particle_graph_bounds(
        {limits.extent_size[0], limits.extent_size[1], limits.extent_size[2]},
        limits.extent_size[3]);
}
void validate_particle_gpu_graph_envelope(const ParticleGpuValues& values,
                                          const ParticleGpuGraphLimits& limits) {
    validate_particle_gpu_graph_limits(limits);
    constexpr double headroom = double(std::numeric_limits<float>::max()) / 16;
    const double size = double(limits.extent_size[3]) * values.acceleration[3];
    for (unsigned row = 0; row < 3; ++row) {
        double envelope = std::abs(double(values.emitter[row][3]));
        for (unsigned column = 0; column < 3; ++column)
            envelope += std::abs(double(values.emitter[row][column])) *
                        (double(limits.extent_size[column]) + size);
        if (!std::isfinite(envelope) || size < 0 || envelope > headroom ||
            !std::isfinite(values.tint[row]) || values.tint[row] < 0 ||
            double(values.tint[row]) * 65504 > headroom)
            throw std::runtime_error("Particle graph instance exceeds finite compute bounds");
    }
}
ParticleGpuValues prepare_particle_gpu_values(const ParticleEffectData& data, double seconds,
                                              std::uint32_t seed, const AffineTransform& world,
                                              const CameraView& camera, double size,
                                              std::array<float, 3> tint) {
    validate_particle_effect(data);
    if (!particle_uses_gpu(data))
        throw std::runtime_error("GPU particle evaluation requires a GPU value profile");
    validate_particle_instance_parameters(size, tint);
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9)
        throw std::runtime_error("GPU particle sample time must be in [0,1e9]");
    gpu_times(data.size_curve);
    gpu_times(data.color_gradient);
    const double phase = seconds * data.spawn_rate, window = data.lifetime * data.spawn_rate;
    std::uint64_t first = 0, last = 0;
    double first_age = seconds, spacing = 0;
    if (data.emission == ParticleEmission::Burst) {
        if (seconds < data.lifetime)
            last = data.burst_count;
    } else {
        last = std::uint64_t(std::floor(phase));
        if (phase >= window) {
            first = std::uint64_t(std::floor(phase - window));
            if (first)
                --first;
        }
        // Resolve double-precision membership before sending only short ages to
        // the GPU. Billion-second clocks never become shader float timestamps.
        while (first < last && phase - double(first + 1) >= window)
            ++first;
        first_age = first < last ? std::min((phase - double(first + 1)) / data.spawn_rate,
                                            std::nextafter(data.lifetime, 0.))
                                 : 0;
        spacing = 1 / data.spawn_rate;
    }
    if (last < first || last - first > data.capacity)
        throw std::runtime_error("GPU particle window exceeds effect capacity");
    ParticleGpuValues result;
    result.births = {std::uint32_t(last - first), std::uint32_t(first), std::uint32_t(first >> 32),
                     seed};
    result.modes = {std::uint32_t(data.shape), std::uint32_t(data.size_curve.size()),
                    std::uint32_t(data.color_gradient.size()), 0};
    result.timing = {gpu(first_age), gpu(spacing), gpu(data.lifetime), gpu(data.drag)};
    const auto axis = unit(data.direction);
    const auto side =
        unit(cross(axis, std::abs(axis[1]) > .9 ? std::array{1., 0., 0.} : std::array{0., 1., 0.}));
    const auto up = cross(axis, side);
    for (unsigned i = 0; i < 3; ++i) {
        result.axis[i] = gpu(axis[i]);
        result.side[i] = gpu(side[i]);
        result.up[i] = gpu(up[i]);
        result.acceleration[i] = gpu(data.acceleration[i]);
        result.shape[i] = gpu(data.shape_size[i]);
        result.tint[i] = tint[i];
        result.colors[0][i] = data.color_start[i];
        result.colors[1][i] = data.color_end[i];
    }
    result.axis[3] = gpu(std::cos(data.cone_angle * std::numbers::pi / 180));
    result.side[3] = gpu(data.speed_min);
    result.up[3] = gpu(data.speed_max);
    result.acceleration[3] = gpu(size);
    result.shape[3] = gpu(data.shape_radius);
    result.sizes = {gpu(data.size_start), gpu(data.size_end), 0, 0};
    double maximum_size = std::max(data.size_start, data.size_end);
    for (std::size_t i = 0; i < data.size_curve.size(); ++i) {
        const auto& stop = data.size_curve[i];
        result.size_stops[i] = {gpu(stop.time), gpu(stop.value), stop.hold ? 1.f : 0.f, 0};
        maximum_size = std::max(maximum_size, stop.value);
    }
    for (std::size_t i = 0; i < data.color_gradient.size(); ++i) {
        const auto& stop = data.color_gradient[i];
        result.color_stops[i] = {gpu(stop.time), stop.value[0], stop.value[1], stop.value[2]};
        if (stop.hold)
            result.modes[3] |= 1u << i;
    }
    if (data.program)
        maximum_size = data.program->maximum_size;
    maximum_size *= size;
    constexpr double headroom = double(std::numeric_limits<float>::max()) / 16;
    for (unsigned row = 0; row < 3; ++row) {
        double envelope = 0;
        for (unsigned column = 0; column < 3; ++column) {
            const auto value = world.m[row * 4 + column];
            result.emitter[row][column] = gpu(value);
            const double origin = data.shape == ParticleSpawnShape::Point ? 0
                                  : data.shape == ParticleSpawnShape::Box
                                      ? .5 * data.shape_size[column]
                                      : data.shape_radius;
            const auto travel =
                origin + data.speed_max * data.lifetime +
                .5 * std::abs(data.acceleration[column]) * data.lifetime * data.lifetime;
            envelope += std::abs(value) *
                        ((data.program ? data.program->extent[column] : travel) + maximum_size);
        }
        const auto translation = world.m[row * 4 + 3] - camera.position[row];
        result.emitter[row][3] = gpu(translation);
        if (!std::isfinite(envelope) || std::abs(translation) + envelope > headroom)
            throw std::runtime_error(
                "GPU particle lifetime envelope exceeds finite compute bounds");
    }
    return result;
}
} // namespace forge
