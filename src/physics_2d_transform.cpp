#include "physics_2d_transform.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace forge::physics_detail {
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool finite(std::array<double, 2> value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]);
}
} // namespace
PlanarMapping::PlanarMapping(const AffineTransform& parent) {
    const auto& m = parent.m;
    for (double value : m)
        require(std::isfinite(value), "Physics XY mapping requires a finite parent transform");
    constexpr double roundoff = 64 * std::numeric_limits<double>::epsilon();
    const double x = std::max({std::abs(m[0]), std::abs(m[4]), std::abs(m[8])});
    const double y = std::max({std::abs(m[1]), std::abs(m[5]), std::abs(m[9])});
    const double z = std::max({std::abs(m[2]), std::abs(m[6]), std::abs(m[10])});
    require(std::abs(m[8]) <= roundoff * x && std::abs(m[9]) <= roundoff * y &&
                std::abs(m[2]) <= roundoff * z && std::abs(m[6]) <= roundoff * z,
            "Physics XY mapping requires an untilted XY parent plane");
    const double scale = std::max({std::abs(m[0]), std::abs(m[1]), std::abs(m[4]), std::abs(m[5])});
    require(scale > 0, "Physics XY mapping has a collapsed XY parent plane");
    const double a = m[0] / scale, b = m[1] / scale;
    const double c = m[4] / scale, d = m[5] / scale;
    const double determinant = a * d - b * c;
    const double norm = std::max(std::abs(a) + std::abs(b), std::abs(c) + std::abs(d));
    const double adjugate_norm = std::max(std::abs(d) + std::abs(b), std::abs(c) + std::abs(a));
    const double rcond = std::abs(determinant) / (norm * adjugate_norm);
    require(std::isfinite(rcond) && rcond >= min_inverse_rcond,
            "Physics XY mapping requires a well-conditioned XY parent plane");
    inverse_ = {d / determinant / scale, -b / determinant / scale, -c / determinant / scale,
                a / determinant / scale};
    for (double value : inverse_)
        require(std::isfinite(value), "Physics XY parent inverse exceeds double precision range");
    origin_ = {m[3], m[7]};
}
std::array<double, 2> PlanarMapping::local_point(std::array<double, 2> world) const {
    require(finite(world), "Physics XY point must be finite");
    // Subtract the existing world origin first; do not introduce another origin
    // owner or form inverse-matrix translation terms that cancel at far positions.
    return local_vector({world[0] - origin_[0], world[1] - origin_[1]});
}
std::array<double, 2> PlanarMapping::local_vector(std::array<double, 2> world) const {
    require(finite(world), "Physics XY displacement must be finite");
    const std::array<double, 2> local{inverse_[0] * world[0] + inverse_[1] * world[1],
                                      inverse_[2] * world[0] + inverse_[3] * world[1]};
    require(finite(local), "Physics XY conversion exceeds double precision range");
    return local;
}
} // namespace forge::physics_detail
