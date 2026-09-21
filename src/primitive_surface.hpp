#pragma once
#include <algorithm>
#include <forge/geometry.hpp>
#include <numbers>

namespace forge::asset_detail {
struct PrimitiveSurfaceVertex {
    std::array<float, 2> uv{};
    std::array<float, 4> tangent{};
};
// Parametric coordinates for the immutable engine shapes. Legacy preview vertex
// layout and geometry remain unchanged. Triangle corners already split UV seams.
inline std::array<PrimitiveSurfaceVertex, 3>
primitive_surface(unsigned kind, const std::array<PrimitiveVertex, 3>& vertices) {
    constexpr float pi = std::numbers::pi_v<float>;
    const bool sphere = kind == 1 || kind == 19;
    const bool revolution =
        kind == 2 || kind == 5 || kind == 6 || kind == 7 || kind == 11 || kind == 18 || kind == 20;
    const bool cap = revolution && std::all_of(vertices.begin(), vertices.end(), [](auto v) {
                         return std::abs(v.normal[1]) > .999999f;
                     });
    std::array<PrimitiveSurfaceVertex, 3> result;
    if (!sphere && (!revolution || cap)) {
        const auto n = vertices[0].normal;
        unsigned axis = 0;
        for (unsigned i = 1; i < 3; ++i)
            if (std::abs(n[i]) > std::abs(n[axis]))
                axis = i;
        const unsigned u = (axis + 1) % 3, v = (axis + 2) % 3;
        // Derivative of the plane position with respect to projected U,
        // holding V constant. Dominant-axis projection keeps division bounded.
        Float3 tangent{};
        tangent[u] = 1;
        tangent[axis] = -n[u] / n[axis];
        const float length = std::sqrt(geom_dot(tangent, tangent));
        for (unsigned i = 0; i < 3; ++i) {
            result[i].uv = {vertices[i].position[u] + .5f, vertices[i].position[v] + .5f};
            for (unsigned j = 0; j < 3; ++j)
                result[i].tangent[j] = tangent[j] / length;
            result[i].tangent[3] = n[axis] < 0 ? -1.f : 1.f;
        }
        return result;
    }
    std::array<bool, 3> pole{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto p = vertices[i].position;
        const auto radius = std::hypot(p[0], p[2]);
        pole[i] = radius < 1e-6f;
        auto& uv = result[i].uv;
        uv[0] = std::atan2(p[2], p[0]) / (2 * pi);
        if (uv[0] < 0)
            uv[0] += 1;
        if (sphere || kind == 18) {
            const auto length = std::hypot(radius, p[1]);
            uv[1] = std::acos(std::clamp(p[1] / length, -1.f, 1.f)) / pi;
        } else if (kind == 11) {
            uv[1] = std::atan2(p[1], radius - .35f) / (2 * pi);
            if (uv[1] < 0)
                uv[1] += 1;
        } else
            uv[1] = p[1] + .5f;
    }
    auto unwrap = [&](unsigned channel, bool ignore_poles) {
        float minimum = 2, maximum = -1;
        for (unsigned i = 0; i < 3; ++i)
            if (!ignore_poles || !pole[i]) {
                minimum = std::min(minimum, result[i].uv[channel]);
                maximum = std::max(maximum, result[i].uv[channel]);
            }
        if (maximum - minimum > .5f)
            for (unsigned i = 0; i < 3; ++i)
                if ((!ignore_poles || !pole[i]) && result[i].uv[channel] < .5f)
                    result[i].uv[channel] += 1;
    };
    unwrap(0, true);
    if (kind == 11)
        unwrap(1, false);
    float pole_u = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!pole[i]) {
            pole_u += result[i].uv[0];
            ++count;
        }
    for (unsigned i = 0; i < 3; ++i) {
        if (pole[i])
            result[i].uv[0] = count ? pole_u / count : 0;
        const float longitude = result[i].uv[0] * 2 * pi;
        Float3 tangent{-std::sin(longitude), 0, std::cos(longitude)};
        const auto n = vertices[i].normal;
        const float along = geom_dot(tangent, n);
        for (unsigned j = 0; j < 3; ++j)
            tangent[j] -= n[j] * along;
        const float length = std::sqrt(geom_dot(tangent, tangent));
        for (unsigned j = 0; j < 3; ++j)
            result[i].tangent[j] = tangent[j] / length;
        const auto p = vertices[i].position;
        result[i].tangent[3] = sphere || kind == 18            ? 1.f
                               : kind == 11 || pole[i]         ? -1.f
                               : n[0] * p[0] + n[2] * p[2] < 0 ? 1.f
                                                               : -1.f;
    }
    return result;
}
} // namespace forge::asset_detail
