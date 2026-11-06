#pragma once
#include "mesh_pick.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace forge {
// One borrowed triangle's authored UV0, independent of its current deformation.
// No source access, retained geometry copy or texture addressing is involved.
struct MeshUvTriangle {
    std::array<std::uint32_t, 3> indices;
    std::array<Double2, 3> uv;
    MeshUvTriangle(const MeshPart& part, std::size_t primitive, MeshPickBudget& budget)
        : indices(mesh_triangle_indices(part, primitive)) {
        const auto* stream = part.find("TEXCOORD_0");
        if (!stream || stream->components != 2)
            throw std::runtime_error("Mesh surface requires float2 UV0");
        const auto* values = std::get_if<std::vector<float>>(&stream->values);
        if (!values || values->size() != std::uint64_t(part.vertices) * 2)
            throw std::runtime_error("Mesh surface has invalid UV0 storage");
        for (unsigned k = 0; k < 3; ++k) {
            budget.spend();
            for (unsigned axis = 0; axis < 2; ++axis) {
                const double value = (*values)[indices[k] * 2ull + axis];
                if (!std::isfinite(value) || value < 0 || value > 1)
                    throw std::runtime_error("Mesh surface UV0 must stay inside [0,1]");
                uv[k][axis] = value;
            }
        }
    }
    Double2 image(Double3 barycentric) const {
        Double2 result{};
        for (unsigned k = 0; k < 3; ++k)
            for (unsigned axis = 0; axis < 2; ++axis)
                result[axis] += barycentric[k] * uv[k][axis];
        return result;
    }
    std::optional<Double3> coordinates(Double2 image) const {
        const double bx = uv[1][0] - uv[0][0], by = uv[1][1] - uv[0][1], cx = uv[2][0] - uv[0][0],
                     cy = uv[2][1] - uv[0][1];
        const double area = bx * cy - by * cx;
        const double scale = std::max({std::abs(bx), std::abs(by), std::abs(cx), std::abs(cy)});
        if (std::abs(area) <= 16 * std::numeric_limits<double>::epsilon() * scale * scale)
            return {};
        const double x = image[0] - uv[0][0], y = image[1] - uv[0][1];
        const double u = (x * cy - y * cx) / area, v = (bx * y - by * x) / area;
        if (!std::isfinite(u) || !std::isfinite(v))
            return {};
        return Double3{1 - u - v, u, v};
    }
    std::array<Double3, 3> relative(const MeshPickVertices& vertices, Double3 origin,
                                    MeshPickBudget& budget) const {
        return {vertices.relative(indices[0], origin, budget),
                vertices.relative(indices[1], origin, budget),
                vertices.relative(indices[2], origin, budget)};
    }
};
inline Double3 mesh_triangle_point(const std::array<Double3, 3>& vertices, Double3 barycentric) {
    Double3 point{};
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] += vertices[k][axis] * barycentric[k];
    return point;
}
inline AffineTransform mesh_triangle_plane(const std::array<Double3, 3>& vertices) {
    AffineTransform plane;
    for (unsigned axis = 0; axis < 3; ++axis) {
        plane.m[axis * 4] = vertices[1][axis] - vertices[0][axis];
        plane.m[axis * 4 + 1] = vertices[2][axis] - vertices[0][axis];
        plane.m[axis * 4 + 3] = vertices[0][axis];
    }
    return plane;
}
} // namespace forge
