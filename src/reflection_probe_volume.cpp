#include "reflection_probe_volume.hpp"
#include "render_values.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace forge {
ReflectionProbeVolume reflection_probe_volume(const ReflectionProbe& value,
                                              const AffineTransform& world) {
    detail::validate_reflection_probe(value);
    if (!std::all_of(world.m.begin(), world.m.end(), [](double x) { return std::isfinite(x); }))
        throw std::invalid_argument("Reflection probe requires a finite world transform");
    auto linear = world;
    linear.m[3] = linear.m[7] = linear.m[11] = 0;
    const auto inverse_linear = inverse(linear);
    ReflectionProbeVolume result;
    result.center = {world.m[3], world.m[7], world.m[11]};
    const double extents[]{value.half_extent_x, value.half_extent_y, value.half_extent_z};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto* row = inverse_linear.m.data() + 4 * axis;
        const double length = std::hypot(row[0], row[1], row[2]);
        const double half = extents[axis] / length;
        if (!std::isfinite(length) || length <= 0 || !std::isfinite(half) ||
            half < std::numeric_limits<float>::min() || half > std::numeric_limits<float>::max())
            throw std::invalid_argument("Reflection probe world volume cannot be represented");
        for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
            result.planes[axis][coordinate] = row[coordinate] / length;
        result.planes[axis][3] = half;
    }
    const double angle = std::remainder(value.rotation_degrees, 360.) * (std::numbers::pi / 180.);
    result.map_rotation = {std::cos(angle), std::sin(angle)};
    return result;
}
std::array<float, 3> reflection_probe_view_center(const ReflectionProbeVolume& volume,
                                                  Double3 origin) {
    std::array<float, 3> result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const double relative = volume.center[axis] - origin[axis];
        if (!std::isfinite(relative) || std::abs(relative) > std::numeric_limits<float>::max())
            throw std::invalid_argument("Reflection probe is outside this camera's numeric range");
        result[axis] = float(relative);
    }
    return result;
}
} // namespace forge
