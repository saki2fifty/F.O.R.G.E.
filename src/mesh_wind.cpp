#include "mesh_wind.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace forge {
namespace {
double height_gradient(const MeshWindSample& sample, const Double3& point) {
    const auto h = (point[1] - sample.base_y) * sample.inverse_height;
    return sample.enabled && h > 0 && h < 1 ? 2 * h * sample.inverse_height : 0;
}
} // namespace
MeshWindSample sample_mesh_wind(const MeshWind& value, const AffineTransform& world,
                                double seconds) {
    validate_mesh_wind(value);
    if (!value.enabled || value.amplitude == 0)
        return {};
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9)
        throw std::runtime_error("Mesh wind sample time must be 0 to 1 billion seconds");
    for (const auto coordinate : world.m)
        if (!std::isfinite(coordinate))
            throw std::runtime_error("Mesh wind requires a finite world transform");
    for (const unsigned axis : {3u, 7u, 11u})
        if (std::abs(world.m[axis]) > 1e9)
            throw std::runtime_error("Mesh wind roots must fit +/-1 billion world meters");
    // Scale before length so even subnormal, nonzero directions retain their angle.
    const auto magnitude = std::max(std::abs(value.direction_x), std::abs(value.direction_z));
    const auto x = value.direction_x / magnitude, z = value.direction_z / magnitude;
    const auto length = std::hypot(x, z);
    const Double2 direction{x / length, z / length};
    const auto spatial =
        (world.m[3] * direction[0] + world.m[11] * direction[1]) / value.wavelength;
    const auto cycles =
        std::remainder(std::remainder(spatial, 1.) - std::remainder(seconds * value.frequency, 1.) +
                           value.phase / 360.,
                       1.);
    const auto amplitude = value.amplitude * std::sin(2 * std::numbers::pi * cycles);
    return {
        true, {amplitude * direction[0], amplitude * direction[1]}, world.m[7], 1 / value.height};
}
Double2 wind_displacement(const MeshWindSample& sample, double height_above_base) {
    if (!sample.enabled)
        return {};
    const auto height = std::clamp(height_above_base * sample.inverse_height, 0., 1.);
    const auto weight = height * height;
    return {sample.displacement[0] * weight, sample.displacement[1] * weight};
}
Double3 wind_point(const MeshWindSample& sample, Double3 point) {
    if (!sample.enabled)
        return point;
    const auto delta = wind_displacement(sample, point[1] - sample.base_y);
    point[0] += delta[0];
    point[2] += delta[1];
    return point;
}
Double3 wind_vector(const MeshWindSample& sample, Double3 point, Double3 vector) {
    if (!sample.enabled)
        return vector;
    const auto derivative = height_gradient(sample, point) * vector[1];
    vector[0] += sample.displacement[0] * derivative;
    vector[2] += sample.displacement[1] * derivative;
    return vector;
}
Double3 wind_normal(const MeshWindSample& sample, Double3 point, Double3 normal) {
    if (!sample.enabled)
        return normal;
    normal[1] -= height_gradient(sample, point) *
                 (sample.displacement[0] * normal[0] + sample.displacement[1] * normal[2]);
    return normal;
}
RenderBounds wind_bounds(const MeshWindSample& sample, const RenderBounds& bounds, Double3 origin) {
    if (!sample.enabled)
        return bounds;
    auto result = bounds;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis] || !std::isfinite(origin[axis]))
            throw std::runtime_error("Mesh wind bounds require finite ordered coordinates");
        const auto displacement =
            axis == 1 ? 0 : std::abs(double(float(sample.displacement[axis == 0 ? 0 : 1])));
        const auto magnitude = std::max(std::abs(bounds.minimum[axis] - origin[axis]),
                                        std::abs(bounds.maximum[axis] - origin[axis])) +
                               displacement;
        // Covers new float conversion/multiply/add and camera-relative subtraction.
        const auto padding = 16 * std::numeric_limits<float>::epsilon() * magnitude +
                             16 * std::numeric_limits<float>::min();
        const auto signed_displacement = axis == 1 ? 0 : sample.displacement[axis == 0 ? 0 : 1];
        result.minimum[axis] =
            std::nextafter(bounds.minimum[axis] + std::min(0., signed_displacement) - padding,
                           -std::numeric_limits<double>::infinity());
        result.maximum[axis] =
            std::nextafter(bounds.maximum[axis] + std::max(0., signed_displacement) + padding,
                           std::numeric_limits<double>::infinity());
        if (!std::isfinite(result.minimum[axis]) || !std::isfinite(result.maximum[axis]))
            throw std::runtime_error("Mesh wind bounds expansion overflowed");
    }
    return result;
}
} // namespace forge
