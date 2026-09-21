#include "render_bounds.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void validate(const RenderBounds& bounds) {
    for (unsigned a = 0; a < 3; ++a)
        require(std::isfinite(bounds.minimum[a]) && std::isfinite(bounds.maximum[a]) &&
                    bounds.minimum[a] <= bounds.maximum[a],
                "Invalid render bounds");
}
using ClipCorners = std::array<std::array<double, 4>, 8>;
ClipCorners clip_corners(const RenderBounds& bounds, const CameraView& camera) {
    validate(bounds);
    ClipCorners corners{};
    for (unsigned corner = 0; corner < 8; ++corner) {
        Double3 relative;
        for (unsigned a = 0; a < 3; ++a)
            relative[a] =
                ((corner & (1u << a)) ? bounds.maximum[a] : bounds.minimum[a]) - camera.position[a];
        const auto view = camera.view.vector(relative);
        for (unsigned r = 0; r < 4; ++r) {
            double value = camera.projection[4 * r + 3];
            for (unsigned c = 0; c < 3; ++c)
                value += camera.projection[4 * r + c] * view[c];
            require(std::isfinite(value), "Render bounds cannot be projected finitely");
            corners[corner][r] = value;
        }
    }
    return corners;
}
} // namespace
RenderBounds transform_bounds(const MeshBounds& bounds, const AffineTransform& world) {
    validate({{bounds.minimum[0], bounds.minimum[1], bounds.minimum[2]},
              {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2]}});
    for (const auto v : world.m)
        require(std::isfinite(v), "Render bounds require a finite world transform");
    RenderBounds result;
    for (unsigned corner = 0; corner < 8; ++corner) {
        Double3 local;
        for (unsigned a = 0; a < 3; ++a)
            local[a] = (corner & (1u << a)) ? bounds.maximum[a] : bounds.minimum[a];
        const auto point = world.point(local);
        for (unsigned a = 0; a < 3; ++a) {
            require(std::isfinite(point[a]), "Transformed render bounds overflow");
            result.minimum[a] = corner ? std::min(result.minimum[a], point[a]) : point[a];
            result.maximum[a] = corner ? std::max(result.maximum[a], point[a]) : point[a];
        }
    }
    return result;
}
bool bounds_visible(const RenderBounds& bounds, const CameraView& camera) {
    const auto corners = clip_corners(bounds, camera);
    for (unsigned plane = 0; plane < 6; ++plane) {
        bool outside = true;
        for (const auto& p : corners) {
            const unsigned coordinate = plane / 2;
            const double distance = coordinate == 2
                                        ? (plane == 4 ? p[2] : p[3] - p[2])
                                        : p[3] + (plane % 2 ? -p[coordinate] : p[coordinate]);
            const double error = 32 * std::numeric_limits<double>::epsilon() *
                                 (std::abs(p[3]) + std::abs(p[coordinate]));
            if (distance >= -error) {
                outside = false;
                break;
            }
        }
        if (outside)
            return false;
    }
    return true;
}
double bounds_camera_depth(const RenderBounds& bounds, const CameraView& camera) {
    validate(bounds);
    double depth = 0;
    for (unsigned a = 0; a < 3; ++a) {
        const double center = (bounds.minimum[a] - camera.position[a]) * .5 +
                              (bounds.maximum[a] - camera.position[a]) * .5;
        depth += center * camera.forward[a];
    }
    require(std::isfinite(depth), "Render bounds camera depth is not finite");
    return depth;
}
float bounds_screen_coverage(const RenderBounds& bounds, const CameraView& camera) {
    require(camera.viewport.width && camera.viewport.height, "LOD needs a nonempty viewport");
    const auto corners = clip_corners(bounds, camera);
    std::array<double, 2> low{}, high{};
    for (unsigned i = 0; i < corners.size(); ++i) {
        const auto& p = corners[i];
        if (p[3] <= 0)
            return 1;
        for (unsigned a = 0; a < 2; ++a) {
            const double v = p[a] / p[3];
            if (!std::isfinite(v))
                return 1; // Finite corners arbitrarily close to the eye plane.
            low[a] = i ? std::min(low[a], v) : v;
            high[a] = i ? std::max(high[a], v) : v;
        }
    }
    return float(
        std::clamp(std::max((high[0] - low[0]) * camera.viewport.width / camera.viewport.height,
                            high[1] - low[1]) *
                       .5,
                   0., 1.));
}
std::size_t select_mesh_lod(const MeshData& mesh, float coverage) {
    require(std::isfinite(coverage) && coverage >= 0 && coverage <= 1, "Invalid screen coverage");
    require(!mesh.lods.empty(), "Mesh has no LODs");
    std::size_t result = 0;
    float previous = 2;
    for (std::size_t i = 0; i < mesh.lods.size(); ++i) {
        const auto threshold = mesh.lods[i].screen_coverage;
        require(std::isfinite(threshold) && threshold >= 0 && threshold < previous &&
                    (i ? true : threshold == 1),
                "Invalid mesh LOD thresholds");
        if (coverage <= threshold)
            result = i;
        previous = threshold;
    }
    return result;
}
} // namespace forge
