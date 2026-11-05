#include "render_bounds.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
namespace {
using Interval = std::array<double, 2>;
constexpr double epsilon = std::numeric_limits<float>::epsilon();
constexpr double minimum = std::numeric_limits<float>::min();
bool gpu_scalar(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return false;
    const auto converted = float(value);
    // A subnormal coefficient times a large position can be normal. Operand
    // flushing would then exceed an output-only rounding pad: decline the view.
    return converted == 0 || std::abs(converted) >= minimum;
}
double magnitude(const Interval& x) { return std::max(std::abs(x[0]), std::abs(x[1])); }
// The shader dot has at most four products and three additions. Sixteen binary32
// epsilons exceed gamma_7, including fused/reassociated evaluation and the double
// interval bookkeeping. The minimum-normal term covers flush-to-zero arithmetic.
double dot_error(const std::array<double, 4>& row, const std::array<Interval, 3>& input) {
    double scale = std::abs(row[3]), coefficients = 1;
    for (unsigned i = 0; i < 3; ++i) {
        scale += std::abs(row[i]) * magnitude(input[i]);
        coefficients += std::abs(row[i]);
    }
    if (!std::isfinite(scale) || scale > std::numeric_limits<float>::max() / 32.)
        return std::numeric_limits<double>::infinity();
    return 16 * epsilon * scale + 32 * minimum * coefficients;
}
std::optional<Interval> dot_interval(const std::array<double, 4>& row,
                                     const std::array<Interval, 3>& input) {
    Interval result{row[3], row[3]};
    for (unsigned i = 0; i < 3; ++i) {
        const double a = row[i] * input[i][0], b = row[i] * input[i][1];
        result[0] += std::min(a, b);
        result[1] += std::max(a, b);
    }
    const double error = dot_error(row, input);
    if (!std::isfinite(error))
        return {};
    return Interval{result[0] - error, result[1] + error};
}
} // namespace
std::optional<MeshOcclusionBounds> mesh_occlusion_bounds(const MeshBounds& source,
                                                         const AffineTransform& world,
                                                         const CameraView& camera) {
    if (!camera.viewport.width || !camera.viewport.height)
        return {};
    std::array<Interval, 3> local{}, relative{}, view{};
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(source.minimum[i]) || !std::isfinite(source.maximum[i]) ||
            source.minimum[i] > source.maximum[i])
            return {};
        local[i] = {double(source.minimum[i]) - minimum, double(source.maximum[i]) + minimum};
    }
    for (unsigned r = 0; r < 3; ++r) {
        std::array<double, 4> row{};
        for (unsigned c = 0; c < 4; ++c) {
            const double value = world.m[4 * r + c] - (c == 3 ? camera.position[r] : 0.);
            if (!gpu_scalar(value))
                return {};
            row[c] = float(value);
        }
        const auto projected = dot_interval(row, local);
        if (!projected)
            return {};
        relative[r] = *projected;
    }
    const Double3* axes[]{&camera.right, &camera.up, &camera.forward};
    for (unsigned r = 0; r < 3; ++r) {
        std::array<double, 4> row{};
        for (unsigned c = 0; c < 3; ++c) {
            if (!gpu_scalar((*axes[r])[c]))
                return {};
            row[c] = float((*axes[r])[c]);
        }
        const auto projected = dot_interval(row, relative);
        if (!projected)
            return {};
        view[r] = *projected;
    }
    std::array<std::array<double, 4>, 4> projection{};
    std::array<double, 4> error{};
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned c = 0; c < 4; ++c) {
            if (!gpu_scalar(camera.projection[4 * r + c]))
                return {};
            projection[r][c] = camera.projection[4 * r + c];
        }
        error[r] = dot_error(projection[r], view);
        if (!std::isfinite(error[r]))
            return {};
    }
    std::array<Interval, 3> ndc{};
    for (unsigned corner = 0; corner < 8; ++corner) {
        std::array<double, 4> clip{};
        for (unsigned r = 0; r < 4; ++r) {
            clip[r] = projection[r][3];
            for (unsigned c = 0; c < 3; ++c)
                clip[r] += projection[r][c] * view[c][bool(corner & (1u << c))];
        }
        const double low_w = clip[3] - error[3], high_w = clip[3] + error[3];
        if (!(low_w > minimum) || !(clip[2] - error[2] > 0) || !(clip[2] + error[2] < low_w))
            return {};
        for (unsigned axis = 0; axis < 3; ++axis) {
            const double lo = clip[axis] - error[axis], hi = clip[axis] + error[axis];
            const double lower = std::min(lo / low_w, lo / high_w);
            const double upper = std::max(hi / low_w, hi / high_w);
            const double pad = 8 * epsilon * (1 + std::max(std::abs(lower), std::abs(upper)));
            if (!std::isfinite(pad))
                return {};
            ndc[axis][0] = corner ? std::min(ndc[axis][0], lower - pad) : lower - pad;
            ndc[axis][1] = corner ? std::max(ndc[axis][1], upper + pad) : upper + pad;
        }
    }
    MeshOcclusionBounds result;
    // One complete pixel also encloses subpixel rasterizer snapping and border
    // coverage. Clamp while still double, before converting viewport coordinates.
    const auto edge = [](double value, unsigned extent, bool high) {
        return std::uint32_t(
            std::clamp(high ? std::ceil(value) + 1 : std::floor(value) - 1, 0., double(extent)));
    };
    result.rectangle = {
        edge((ndc[0][0] + 1) * .5 * camera.viewport.width, camera.viewport.width, false),
        edge((1 - ndc[1][1]) * .5 * camera.viewport.height, camera.viewport.height, false),
        edge((ndc[0][1] + 1) * .5 * camera.viewport.width, camera.viewport.width, true),
        edge((1 - ndc[1][0]) * .5 * camera.viewport.height, camera.viewport.height, true)};
    if (result.rectangle[0] >= result.rectangle[2] || result.rectangle[1] >= result.rectangle[3] ||
        ndc[2][0] <= 0 || ndc[2][0] >= 1)
        return {};
    result.nearest_depth = std::nextafter(float(ndc[2][0]), 0.f);
    return result;
}
} // namespace forge
