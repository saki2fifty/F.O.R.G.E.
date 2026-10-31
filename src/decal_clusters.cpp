#include "decal_clusters.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge {
std::optional<DecalClusters> prepare_decal_clusters(std::span<const DecalTransformRows> transforms,
                                                    const RenderBounds& bounds, Double3 origin) {
    if (transforms.empty())
        return {};
    if (transforms.size() > decal_scene_limit)
        throw std::runtime_error("Projector spatial input limit exceeded");
    for (const auto& matrix : transforms)
        for (const auto& row : matrix)
            for (float value : row)
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite GPU projector row");
    constexpr double epsilon = std::numeric_limits<float>::epsilon();
    constexpr double tiny = std::numeric_limits<float>::min();
    constexpr double maximum = std::numeric_limits<float>::max();
    DecalClusters result;
    std::array<double, 3> coordinate_error{};
    unsigned active_axes = 0;
    for (unsigned a = 0; a < 3; ++a) {
        const double low = bounds.minimum[a] - origin[a], high = bounds.maximum[a] - origin[a];
        if (!std::isfinite(low) || !std::isfinite(high) || low > high || low < -maximum ||
            high > maximum)
            return {};
        float minimum = float(low), upper = float(high);
        if (double(minimum) > low)
            minimum = std::nextafter(minimum, -std::numeric_limits<float>::infinity());
        if (double(upper) < high)
            upper = std::nextafter(upper, std::numeric_limits<float>::infinity());
        if (!std::isfinite(minimum) || !std::isfinite(upper))
            return {};
        result.controls[0][a] = minimum;
        result.controls[1][a] = upper;
        const double span = double(upper) - minimum;
        if (span == 0)
            continue;
        const float scale = float(decal_cluster_side / span);
        if (!std::isfinite(scale) || scale < tiny)
            continue;
        // Shader uses precise p*scale - minimum*scale. Include roundoff and
        // possible input/result denormal flushing; poor axes collapse to cell0.
        const double magnitude = (std::max(std::abs(double(minimum)), std::abs(double(upper))) +
                                  std::abs(double(minimum))) *
                                 scale;
        const double error = 8 * epsilon * magnitude + 2 * tiny * scale + 32 * tiny;
        if (!std::isfinite(error) || error >= .25)
            continue;
        result.controls[2][a] = scale;
        coordinate_error[a] = error;
        ++active_axes;
    }
    if (!active_axes)
        return {};
    for (unsigned cell = 0; cell < decal_cluster_cells; ++cell) {
        const std::array<unsigned, 3> coord{cell % decal_cluster_side,
                                            (cell / decal_cluster_side) % decal_cluster_side,
                                            cell / (decal_cluster_side * decal_cluster_side)};
        std::array<Double3, 2> region{};
        bool reachable = true;
        for (unsigned a = 0; a < 3; ++a) {
            const double low = result.controls[0][a], high = result.controls[1][a];
            const double scale = result.controls[2][a];
            if (scale == 0) {
                if (coord[a])
                    reachable = false;
                region[0][a] = low;
                region[1][a] = high;
            } else {
                const double pad = coordinate_error[a];
                region[0][a] =
                    std::max(low, std::nextafter(low + (double(coord[a]) - pad) / scale,
                                                 -std::numeric_limits<double>::infinity()));
                region[1][a] =
                    coord[a] == decal_cluster_side - 1
                        ? high
                        : std::min(high, std::nextafter(low + (double(coord[a] + 1) + pad) / scale,
                                                        std::numeric_limits<double>::infinity()));
            }
        }
        const auto start = std::uint32_t(result.indices.size());
        if (reachable)
            for (unsigned i = 0; i < transforms.size(); ++i) {
                bool intersects = true, possible_overflow = false;
                for (const auto& row : transforms[i]) {
                    double low = row[3], high = row[3], magnitude = std::abs(double(row[3]));
                    double flush = 32 * tiny;
                    for (unsigned a = 0; a < 3; ++a) {
                        const double coefficient = row[a];
                        const double p = std::max(std::abs(region[0][a]), std::abs(region[1][a]));
                        const double x = coefficient * region[0][a], y = coefficient * region[1][a];
                        low += std::min(x, y);
                        high += std::max(x, y);
                        magnitude += std::abs(coefficient) * p;
                        flush += std::abs(coefficient) * tiny;
                        if (std::abs(coefficient) < tiny)
                            flush += std::abs(coefficient) * p;
                    }
                    // A possible float overflow must still reach the ordinary
                    // projector diagnostic, rather than being hidden by culling.
                    if (magnitude >= maximum * (1 - 16 * epsilon)) {
                        possible_overflow = true;
                        continue;
                    }
                    const double error = 16 * epsilon * magnitude + flush;
                    if (low - error > .5 || high + error < -.5) {
                        intersects = false;
                    }
                }
                if (intersects || possible_overflow)
                    result.indices.push_back(i);
            }
        result.ranges[cell] = {start, std::uint32_t(result.indices.size()) - start};
    }
    if (result.indices.size() > decal_cluster_cells * decal_scene_limit)
        throw std::runtime_error("Projector grid payload exceeded");
    return result;
}
} // namespace forge
