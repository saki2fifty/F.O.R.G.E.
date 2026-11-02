#pragma once
#include "tile_set_values.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/transform.hpp>
#include <stdexcept>
#include <vector>

namespace forge {
struct TileMapCollisionGeometry;
}
namespace forge::physics_detail {
struct BoxPart2D {
    std::array<float, 2> center{}, dimensions{}; // Scaled body-local metres; full dimensions.
    bool operator==(const BoxPart2D&) const = default;
};
struct PolygonPart2D {
    std::array<std::array<float, 2>, tile_collision_vertex_limit> points{};
    std::uint32_t count = 0;
    std::uint8_t boundary_edges = 0xff; // Original edges in the input point order.
    bool operator==(const PolygonPart2D&) const = default;
};
struct ColliderParts2D {
    std::vector<BoxPart2D> boxes;
    std::vector<PolygonPart2D> polygons;
};
// The existing quaternion projection shared with native realization. Values
// are cos/sin, not a separately authored angle or transform.
inline std::array<float, 2> collider_rotation_2d(LocalRotation value) {
    const auto q = normalized(value);
    const double length = std::hypot(double(q.z), double(q.w));
    const double z = q.z / length, w = q.w / length;
    return {float(w * w - z * z), float(2 * w * z)};
}
// Pure value admission shared by physical realization and detached geometry
// consumers. Native shape/mass checks remain with the physics owner.
inline float collider_dimension_2d(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("2D effective collider dimensions exceed the supported profile");
    return float(value);
}
inline std::array<float, 2> collider_dimensions_2d(unsigned shape, std::array<float, 2> raw,
                                                   LocalScale scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z))
        throw std::runtime_error("2D collider scale must be finite");
    const double x = std::abs(scale.x), y = std::abs(scale.y);
    if (shape == 0)
        return {collider_dimension_2d(raw[0] * x, .01f, 1000),
                collider_dimension_2d(raw[1] * y, .01f, 1000)};
    if (shape > 2)
        throw std::runtime_error("Unknown 2D collider kind");
    if (std::abs(x - y) > 1e-6 * std::max(x, y))
        throw std::runtime_error("Circle/Capsule Collider 2D requires equal absolute XY scale");
    std::array<float, 2> dimensions{collider_dimension_2d(raw[0] * x, .005f, 500), 0};
    if (shape == 2) {
        dimensions[1] = collider_dimension_2d(raw[1] * y, .01f, 1000);
        if (double(dimensions[1]) + 2 * double(dimensions[0]) > 1000)
            throw std::runtime_error("2D capsule total height exceeds 1000 metres");
    } else if (raw[1] != 0)
        throw std::runtime_error("Circle Collider 2D has an unused height");
    return dimensions;
}
ColliderParts2D project_tile_collision_2d(const TileMapCollisionGeometry&,
                                          std::array<double, 2> cell_size, LocalScale);
} // namespace forge::physics_detail
